package reservation

import (
	"context"
	"fmt"
	"net"
	"strconv"
	"time"

	"github.com/bradfitz/gomemcache/memcache"
	"hotelreservation/registry"
	pb "hotelreservation/services/reservation/proto"
	"hotelreservation/tls"
	"github.com/google/uuid"
	"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/opentracing/opentracing-go"
	"github.com/rs/zerolog/log"
	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"
)

const name = "srv-reservation"

// Server implements the user service
type Server struct {
	pb.UnimplementedReservationServer

	uuid string

	Tracer      opentracing.Tracer
	Port        int
	IpAddr      string
	MongoClient *mongo.Client
	Registry    *registry.Client
	MemcClient  *memcache.Client
}

// Run starts the server
func (s *Server) Run() error {
	opentracing.SetGlobalTracer(s.Tracer)

	if s.Port == 0 {
		return fmt.Errorf("server port must be set")
	}

	s.uuid = uuid.New().String()

	opts := []grpc.ServerOption{
		grpc.KeepaliveParams(keepalive.ServerParameters{
			Timeout: 120 * time.Second,
		}),
		grpc.KeepaliveEnforcementPolicy(keepalive.EnforcementPolicy{
			PermitWithoutStream: true,
		}),
		grpc.UnaryInterceptor(
			otgrpc.OpenTracingServerInterceptor(s.Tracer),
		),
	}

	if tlsopt := tls.GetServerOpt(); tlsopt != nil {
		opts = append(opts, tlsopt)
	}

	srv := grpc.NewServer(opts...)

	pb.RegisterReservationServer(srv, s)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.Port))
	if err != nil {
		log.Fatal().Msgf("failed to listen: %v", err)
	}

	log.Trace().Msgf("In reservation s.IpAddr = %s, port = %d", s.IpAddr, s.Port)

	err = s.Registry.Register(name, s.uuid, s.IpAddr, s.Port)
	if err != nil {
		return fmt.Errorf("failed register: %v", err)
	}
	log.Info().Msg("Successfully registered in consul")

	return srv.Serve(lis)
}

// Shutdown cleans up any processes
func (s *Server) Shutdown() {
	s.Registry.Deregister(s.uuid)
}

// MakeReservation makes a reservation based on given information
func (s *Server) MakeReservation(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	res := new(pb.Result)
	res.HotelId = make([]string, 0)

	database := s.MongoClient.Database("reservation-db")
	resCollection := database.Collection("reservation")
	numCollection := database.Collection("number")

	inDate, _ := time.Parse(
		time.RFC3339,
		req.InDate+"T12:00:00+00:00")

	outDate, _ := time.Parse(
		time.RFC3339,
		req.OutDate+"T12:00:00+00:00")
	hotelId := req.HotelId[0]

	indate := inDate.String()[0:10]

	memc_date_num_map := make(map[string]int)

	for inDate.Before(outDate) {
		// check reservations
		count := 0
		inDate = inDate.AddDate(0, 0, 1)
		outdate := inDate.String()[0:10]

		// first check memc
		memc_key := hotelId + "_" + inDate.String()[0:10] + "_" + outdate
		item, err := s.MemcClient.Get(memc_key)
		if err == nil {
			// memcached hit
			count, _ = strconv.Atoi(string(item.Value))
			log.Trace().Msgf("memcached hit %s = %d", memc_key, count)
			memc_date_num_map[memc_key] = count + int(req.RoomNumber)

		} else if err == memcache.ErrCacheMiss {
			// memcached miss
			log.Trace().Msgf("memcached miss")
			var reserve []reservation

			filter := bson.D{{"hotelId", hotelId}, {"inDate", indate}, {"outDate", outdate}}
			curr, err := resCollection.Find(context.TODO(), filter)
			if err != nil {
				log.Error().Msgf("Failed get reservation data: ", err)
			}
			curr.All(context.TODO(), &reserve)
			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v] from date [%v] to date [%v], but got error", hotelId, indate, outdate, err.Error())
			}

			for _, r := range reserve {
				count += r.Number
			}

			memc_date_num_map[memc_key] = count + int(req.RoomNumber)

		} else {
			log.Panic().Msgf("Tried to get memc_key [%v], but got memmcached error = %s", memc_key, err)
		}

		// check capacity
		// check memc capacity
		memc_cap_key := hotelId + "_cap"
		item, err = s.MemcClient.Get(memc_cap_key)
		hotel_cap := 0
		if err == nil {
			// memcached hit
			hotel_cap, _ = strconv.Atoi(string(item.Value))
			log.Trace().Msgf("memcached hit %s = %d", memc_cap_key, hotel_cap)
		} else if err == memcache.ErrCacheMiss {
			// memcached miss
			var num number
			err = numCollection.FindOne(context.TODO(), &bson.D{{"hotelId", hotelId}}).Decode(&num)
			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v], but got error", hotelId, err.Error())
			}
			hotel_cap = int(num.Number)

			// write to memcache
			s.MemcClient.Set(&memcache.Item{Key: memc_cap_key, Value: []byte(strconv.Itoa(hotel_cap))})
		} else {
			log.Panic().Msgf("Tried to get memc_cap_key [%v], but got memmcached error = %s", memc_cap_key, err)
		}

		if count+int(req.RoomNumber) > hotel_cap {
			return res, nil
		}
		indate = outdate
	}

	// only update reservation number cache after check succeeds
	for key, val := range memc_date_num_map {
		s.MemcClient.Set(&memcache.Item{Key: key, Value: []byte(strconv.Itoa(val))})
	}

	inDate, _ = time.Parse(
		time.RFC3339,
		req.InDate+"T12:00:00+00:00")

	indate = inDate.String()[0:10]

	for inDate.Before(outDate) {
		inDate = inDate.AddDate(0, 0, 1)
		outdate := inDate.String()[0:10]
		_, err := resCollection.InsertOne(
			context.TODO(),
			reservation{
				HotelId:      hotelId,
				CustomerName: req.CustomerName,
				InDate:       indate,
				OutDate:      outdate,
				Number:       int(req.RoomNumber),
			},
		)
		if err != nil {
			log.Panic().Msgf("Tried to insert hotel [hotelId %v], but got error", hotelId, err.Error())
		}
		indate = outdate
	}

	res.HotelId = append(res.HotelId, hotelId)

	return res, nil
}

// CheckAvailability checks if given information is available
func (s *Server) CheckAvailability(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	res := new(pb.Result)
	res.HotelId = make([]string, 0)

	type dayWindow struct {
		indate  string
		outdate string
		memcKey string
	}

	resMap := make(map[string]bool)
	hotelMemKeys := make([]string, 0, len(req.HotelId))
	cacheCap := make(map[string]int)
	numCollection := s.MongoClient.Database("reservation-db").Collection("number")
	resCollection := s.MongoClient.Database("reservation-db").Collection("reservation")

	for _, hotelId := range req.HotelId {
		resMap[hotelId] = true
		hotelMemKeys = append(hotelMemKeys, hotelId+"_cap")
	}

	capMemSpan, _ := opentracing.StartSpanFromContext(ctx, "memcached_capacity_get_multi_number_lite")
	capMemSpan.SetTag("span.kind", "client")
	cacheMemRes, capErr := s.MemcClient.GetMulti(hotelMemKeys)
	capMemSpan.Finish()
	if capErr != nil && capErr != memcache.ErrCacheMiss {
		log.Panic().Msgf("Tried to get memc_cap_key [%v], but got memmcached error = %s", hotelMemKeys, capErr)
	}

	for key, val := range cacheMemRes {
		hotelId := key[:len(key)-4]
		hotelCap, _ := strconv.Atoi(string(val.Value))
		cacheCap[hotelId] = hotelCap
	}

	for _, hotelId := range req.HotelId {
		log.Trace().Msgf("reservation check hotel %s", hotelId)

		hotelCap, ok := cacheCap[hotelId]
		if !ok {
			var num number
			capMongoSpan, _ := opentracing.StartSpanFromContext(ctx, "mongodb_capacity_get_number")
			capMongoSpan.SetTag("span.kind", "client")
			err := numCollection.FindOne(context.TODO(), bson.D{{"hotelId", hotelId}}).Decode(&num)
			capMongoSpan.Finish()
			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v], but got error", hotelId, err.Error())
			}
			hotelCap = num.Number
			_ = s.MemcClient.Set(&memcache.Item{Key: hotelId + "_cap", Value: []byte(strconv.Itoa(hotelCap))})
			cacheCap[hotelId] = hotelCap
		}

		inDate, _ := time.Parse(
			time.RFC3339,
			req.InDate+"T12:00:00+00:00")
		outDate, _ := time.Parse(
			time.RFC3339,
			req.OutDate+"T12:00:00+00:00")

		windows := make([]dayWindow, 0)
		memcKeys := make([]string, 0)
		for inDate.Before(outDate) {
			indate := inDate.String()[:10]
			inDate = inDate.AddDate(0, 0, 1)
			outdate := inDate.String()[:10]
			memcKey := hotelId + "_" + outdate + "_" + outdate
			windows = append(windows, dayWindow{indate: indate, outdate: outdate, memcKey: memcKey})
			memcKeys = append(memcKeys, memcKey)
		}

		reserveMemSpan, _ := opentracing.StartSpanFromContext(ctx, "memcached_reserve_get_multi_number_lite")
		reserveMemSpan.SetTag("span.kind", "client")
		itemsMap, memErr := s.MemcClient.GetMulti(memcKeys)
		reserveMemSpan.Finish()
		if memErr != nil && memErr != memcache.ErrCacheMiss {
			log.Panic().Msgf("Tried to get memc_key [%v], but got memmcached error = %s", memcKeys, memErr)
		}

		countByKey := make(map[string]int, len(memcKeys))
		missWindows := make([]dayWindow, 0)
		for _, w := range windows {
			if item, ok := itemsMap[w.memcKey]; ok {
				val, _ := strconv.Atoi(string(item.Value))
				countByKey[w.memcKey] = val
			} else {
				missWindows = append(missWindows, w)
			}
		}

		if len(missWindows) > 0 {
			orFilters := make([]bson.D, 0, len(missWindows))
			for _, w := range missWindows {
				orFilters = append(orFilters, bson.D{{"inDate", w.indate}, {"outDate", w.outdate}})
			}

			var reserve []reservation
			filter := bson.D{{"hotelId", hotelId}, {"$or", orFilters}}
			reserveMongoSpan, _ := opentracing.StartSpanFromContext(ctx, "mongodb_reserve_get_multi_number_lite")
			reserveMongoSpan.SetTag("span.kind", "client")
			curr, err := resCollection.Find(context.TODO(), filter)
			if err != nil {
				log.Error().Msgf("Failed get reservation data: ", err)
			}
			curr.All(context.TODO(), &reserve)
			if err != nil {
				log.Error().Msgf("Failed get reservation data: ", err)
			}
			reserveMongoSpan.Finish()

			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v], but got error", hotelId, err.Error())
			}

			for _, w := range missWindows {
				countByKey[w.memcKey] = 0
			}
			for _, r := range reserve {
				k := hotelId + "_" + r.OutDate + "_" + r.OutDate
				countByKey[k] += r.Number
			}

			for _, w := range missWindows {
				_ = s.MemcClient.Set(&memcache.Item{Key: w.memcKey, Value: []byte(strconv.Itoa(countByKey[w.memcKey]))})
			}
		}

		for _, w := range windows {
			if countByKey[w.memcKey]+int(req.RoomNumber) > hotelCap {
				resMap[hotelId] = false
				break
			}
		}
	}

	for k, v := range resMap {
		if v {
			res.HotelId = append(res.HotelId, k)
		}
	}

	return res, nil
}

type reservation struct {
	HotelId      string `bson:"hotelId"`
	CustomerName string `bson:"customerName"`
	InDate       string `bson:"inDate"`
	OutDate      string `bson:"outDate"`
	Number       int    `bson:"number"`
}

type number struct {
	HotelId string `bson:"hotelId"`
	Number  int    `bson:"numberOfRoom"`
}
