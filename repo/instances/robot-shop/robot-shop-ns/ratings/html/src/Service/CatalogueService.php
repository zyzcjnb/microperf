<?php

declare(strict_types=1);

namespace monitoring\RobotShop\Ratings\Service;

use Exception;
use Psr\Log\LoggerAwareInterface;
use Psr\Log\LoggerAwareTrait;

class CatalogueService implements LoggerAwareInterface
{
    use LoggerAwareTrait;

    /**
     * @var string
     */
    private $catalogueUrl;

    /**
     * Simple per-process TTL cache for SKU existence checks.
     * Product catalogue is effectively static in this demo, so a short
     * TTL dramatically reduces repeated HTTP calls to the catalogue service.
     *
     * @var array<string, array{result: bool, expires: int}>
     */
    private static $skuCache = [];

    private static $cacheTtlSeconds = 30;

    public function __construct(string $catalogueUrl)
    {
        $this->catalogueUrl = $catalogueUrl;
    }

    public function checkSKU(string $sku): bool
    {
        $now = time();
        if (isset(self::$skuCache[$sku])) {
            $entry = self::$skuCache[$sku];
            if ($entry['expires'] > $now) {
                $this->logger->info("catalogue cache hit for sku $sku");
                return $entry['result'];
            }
            unset(self::$skuCache[$sku]);
        }

        $url = sprintf('%s/product/%s', $this->catalogueUrl, $sku);

        $opt = [
            CURLOPT_RETURNTRANSFER => true,
        ];
        $curl = curl_init($url);
        curl_setopt_array($curl, $opt);

        $data = curl_exec($curl);
        if (!$data) {
            $this->logger->error('failed to connect to catalogue');
            throw new Exception('Failed to connect to catalogue');
        }

        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        $this->logger->info("catalogue status $status");

        curl_close($curl);

        $result = 200 === $status;
        self::$skuCache[$sku] = ['result' => $result, 'expires' => $now + self::$cacheTtlSeconds];

        return $result;
    }
}
