<?php

namespace monitoring\RobotShop\Ratings\EventListener;

use monitoring\monitoringRuntimeException;
use monitoring\Tracer;
use Psr\Log\LoggerInterface;

class monitoringDataCenterListener
{
    private static $dataCenters = [
        "asia-northeast2",
        "asia-south1",
        "europe-west3",
        "us-east1",
        "us-west1"
    ];

    /**
     * @var LoggerInterface
     */
    private $logger;

    public function __construct(LoggerInterface $logger)
    {
        $this->logger = $logger;
    }

    public function __invoke()
    {
        try {
            $entry = Tracer::getEntrySpan();

            $dataCenter = self::$dataCenters[array_rand(self::$dataCenters)];
            $entry->annotate('datacenter', $dataCenter);

            $this->logger->info(sprintf('Annotated DataCenter %s', $dataCenter));
        } catch (monitoringRuntimeException $exception) {
            $this->logger->error('Unable to annotate entry span: %s', $exception->getMessage());
        }
    }
}
