package recommendationcompute

import (
	"context"
	"fmt"
	"net"
	"time"

	"hotelreservation/dialer"
	"hotelreservation/registry"
	_ "github.com/mbobakov/grpc-consul-resolver"
	pb "hotelreservation/services/recommendation/proto"
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

const name = "srv-recommendation-compute"

// Server implements the compute-layer recommendation service.
// It calls the store service, performs pointless post-processing,
// and writes an audit log to MongoDB on every request.
type Server struct {
	pb.UnimplementedRecommendationServer

	storeClient pb.RecommendationClient
	uuid        string
	Tracer      opentracing.Tracer
	Port        int
	IpAddr      string
	MongoClient *mongo.Client
	Registry    *registry.Client
}

// Run starts the server
func (s *Server) Run() error {
	if s.Port == 0 {
		return fmt.Errorf("server port must be set")
	}

	if err := s.initStoreClient("srv-recommendation-store"); err != nil {
		return err
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

func (s *Server) initStoreClient(name string) error {
	conn, err := dialer.Dial(
		fmt.Sprintf("consul://%s/%s", "consul:8500", name),
		dialer.WithTracer(s.Tracer),
		dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.storeClient = pb.NewRecommendationClient(conn)
	return nil
}

// Shutdown cleans up any processes
func (s *Server) Shutdown() {
	s.Registry.Deregister(s.uuid)
}

// GetRecommendations calls the store service, does pointless post-processing,
// and writes an audit record to MongoDB.
func (s *Server) GetRecommendations(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	log.Trace().Msgf("[compute] GetRecommendations")

	// Step 1: call downstream store service
	storeRes, err := s.storeClient.GetRecommendations(ctx, &pb.Request{
		Require: req.Require,
		Lat:     req.Lat,
		Lon:     req.Lon,
	})
	if err != nil {
		return nil, err
	}

	// Step 2: pointless but "realistic-looking" post-processing
	// Simulate a "ranking algorithm v2" that shuffles and re-sorts
	// In reality this does nothing meaningful.
	processedIds := make([]string, len(storeRes.HotelIds))
	copy(processedIds, storeRes.HotelIds)
	// Reverse the slice to simulate "re-ranking"
	for i, j := 0, len(processedIds)-1; i < j; i, j = i+1, j-1 {
		processedIds[i], processedIds[j] = processedIds[j], processedIds[i]
	}

	// Step 3: write audit log to MongoDB on every call
	s.writeAuditLog(ctx, req.Require, len(processedIds))

	res := new(pb.Result)
	res.HotelIds = processedIds
	return res, nil
}

// writeAuditLog writes an audit entry to the database for every
// microservice independently logs its actions.
func (s *Server) writeAuditLog(ctx context.Context, require string, resultCount int) {
	collection := s.MongoClient.Database("recommendation-db").Collection("compute_audit")
	_, err := collection.InsertOne(ctx, bson.D{
		{"service", "recommendation-compute"},
		{"require", require},
		{"result_count", resultCount},
		{"timestamp", time.Now().UnixNano()},
	})
	if err != nil {
		log.Error().Msgf("[compute] Failed to write audit log: %v", err)
	}
}
