#!/usr/bin/env python3
"""Seed Pitstop with test customers and vehicles via backend APIs."""
import argparse
import random
import sys

import requests


def random_id(prefix: str, n: int) -> str:
    return f"{prefix}{n:05d}"


def random_email(n: int) -> str:
    return f"customer{n:05d}@pitstop.perf"


def random_phone(n: int) -> str:
    return f"555-{n:05d}"


def license_for_index(n: int) -> str:
    """Generate a deterministic, unique license number matching the system's format."""
    return f"{n:03d}-{n:03d}-{n:03d}"


def ensure_entity(base_url: str, entity_url: str, payload: dict, desc: str):
    """POST an entity. If it already exists (detected via GET or duplicate-key 500), treat as success."""
    try:
        r = requests.post(base_url + entity_url, json=payload, timeout=30)
        if r.status_code in (201, 200):
            return True

        # If creation failed, check whether the entity already exists (e.g. previous
        # request timed out but was actually committed).
        if r.status_code == 500:
            if "Customer" in entity_url and "CustomerId" in payload:
                check = requests.get(f"{base_url}/api/customers/{payload['CustomerId']}", timeout=10)
                if check.status_code == 200:
                    return True
            elif "Vehicle" in entity_url and "LicenseNumber" in payload:
                check = requests.get(f"{base_url}/api/vehicles/{payload['LicenseNumber']}", timeout=10)
                if check.status_code == 200:
                    return True

        print(f"Failed to {desc}: {r.status_code} {r.text[:500]}", file=sys.stderr)
        return False
    except Exception as ex:
        print(f"Failed to {desc}: {ex}", file=sys.stderr)
        return False


def seed_customers(base_url: str, count: int):
    for i in range(count):
        payload = {
            "CustomerId": random_id("CUST", i),
            "Name": f"Performance Customer {i}",
            "Address": f"Street {i}",
            "PostalCode": f"{10000 + (i % 90000):05d}",
            "City": "Test City",
            "TelephoneNumber": random_phone(i),
            "EmailAddress": random_email(i),
        }
        if not ensure_entity(base_url, "/api/customers", payload, f"create customer {i}"):
            sys.exit(1)
    print(f"Seeded {count} customers.")


def seed_vehicles(base_url: str, count: int):
    brands = ["Toyota", "Honda", "Ford", "BMW", "Audi", "Tesla", "Volvo"]
    types = ["Sedan", "SUV", "Hatchback", "Truck", "Coupe"]
    for i in range(count):
        payload = {
            "LicenseNumber": license_for_index(i),
            "Brand": random.choice(brands),
            "Type": random.choice(types),
            "OwnerId": random_id("CUST", i % count),
        }
        if not ensure_entity(base_url, "/api/vehicles", payload, f"create vehicle {i}"):
            sys.exit(1)
    print(f"Seeded {count} vehicles.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--customer-api", default="http://localhost:5100")
    parser.add_argument("--vehicle-api", default="http://localhost:5001")
    parser.add_argument("--count", type=int, default=500)
    args = parser.parse_args()

    print("Seeding test data...", flush=True)
    seed_customers(args.customer_api, args.count)
    seed_vehicles(args.vehicle_api, args.count)
    print("Seeding complete.", flush=True)


if __name__ == "__main__":
    main()
