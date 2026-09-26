package recommendationstore

import (
	"context"
	"fmt"
	"math"
	"net"
	"sync"
	"time"

	"hotelreservation/registry"
	pb "hotelreservation/services/recommendation/proto"
	"hotelreservation/tls"
	"github.com/google/uuid"
	"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/hailocab/go-geoindex"
	"github.com/opentracing/opentracing-go"
	"github.com/rs/zerolog/log"
	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"
)

const name = "srv-recommendation-store"

// Server implements the data-layer recommendation service.
// It queries MongoDB and performs the actual filtering logic.
type Server struct {
	pb.UnimplementedRecommendationServer

	uuid   string
	Tracer      opentracing.Tracer
	Port        int
	IpAddr      string
	MongoClient *mongo.Client
	Registry    *registry.Client

	// dbMutex serializes database access across concurrent requests
	dbMutex sync.Mutex
}

// Run starts the server
func (s *Server) Run() error {
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

	pb.RegisterRecommendationServer(srv, s)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.Port))
	if err != nil {
		log.Fatal().Msgf("failed to listen: %v", err)
	}

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

// GetRecommendations loads hotels from MongoDB and filters by requirement.
func (s *Server) GetRecommendations(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	res := new(pb.Result)
	log.Trace().Msgf("[store] GetRecommendations")

	hotels := s.loadHotelsFromDB(ctx)
	require := req.Require

	if require == "dis" {
		p1 := &geoindex.GeoPoint{
			Pid:  "",
			Plat: req.Lat,
			Plon: req.Lon,
		}
		min := math.MaxFloat64
		for _, hotel := range hotels {
			tmp := float64(geoindex.Distance(p1, &geoindex.GeoPoint{
				Pid:  "",
				Plat: hotel.HLat,
				Plon: hotel.HLon,
			})) / 1000
			if tmp < min {
				min = tmp
			}
		}
		for _, hotel := range hotels {
			tmp := float64(geoindex.Distance(p1, &geoindex.GeoPoint{
				Pid:  "",
				Plat: hotel.HLat,
				Plon: hotel.HLon,
			})) / 1000
			if tmp == min {
				res.HotelIds = append(res.HotelIds, hotel.HId)
			}
		}
	} else if require == "rate" {
		max := 0.0
		for _, hotel := range hotels {
			if hotel.HRate > max {
				max = hotel.HRate
			}
		}
		for _, hotel := range hotels {
			if hotel.HRate == max {
				res.HotelIds = append(res.HotelIds, hotel.HId)
			}
		}
	} else if require == "price" {
		min := math.MaxFloat64
		for _, hotel := range hotels {
			if hotel.HPrice < min {
				min = hotel.HPrice
			}
		}
		for _, hotel := range hotels {
			if hotel.HPrice == min {
				res.HotelIds = append(res.HotelIds, hotel.HId)
			}
		}
	} else {
		log.Warn().Msgf("[store] Wrong require parameter: %v", require)
	}

	return res, nil
}

// loadHotelsFromDB queries MongoDB for all hotels on every request.
func (s *Server) loadHotelsFromDB(ctx context.Context) []Hotel {
	s.dbMutex.Lock()
	defer s.dbMutex.Unlock()
	collection := s.MongoClient.Database("hotel-shared-db").Collection("recommendation")
	curr, err := collection.Find(ctx, bson.D{})
	if err != nil {
		log.Error().Msgf("[store] Failed to get hotels data: %v", err)
		return nil
	}
	defer curr.Close(ctx)

	var hotels []Hotel
	if err := curr.All(ctx, &hotels); err != nil {
		log.Error().Msgf("[store] Failed to decode hotels data: %v", err)
		return nil
	}
	return hotels
}

type Hotel struct {
	HId    string  `bson:"hotelId"`
	HLat   float64 `bson:"lat"`
	HLon   float64 `bson:"lon"`
	HRate  float64 `bson:"rate"`
	HPrice float64 `bson:"price"`
}
