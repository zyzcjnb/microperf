package main

import (
	"context"
	"fmt"

	"github.com/rs/zerolog/log"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
)

func initializeDatabase(url string) (*mongo.Client, func()) {
	uri := fmt.Sprintf("mongodb://%s", url)
	log.Info().Msgf("Attempting connection to %v", uri)

	opts := options.Client().ApplyURI(uri).SetMaxPoolSize(10)
	client, err := mongo.Connect(context.TODO(), opts)
	if err != nil {
		log.Panic().Msg(err.Error())
	}
	log.Info().Msg("Successfully connected to MongoDB")

	return client, func() {
		if err := client.Disconnect(context.TODO()); err != nil {
			log.Fatal().Msg(err.Error())
		}
	}
}
