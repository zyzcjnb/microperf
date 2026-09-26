#!/usr/bin/env python3
"""Seed Pitstop by generating and executing SQL INSERT statements inside the SQL Server container."""
import argparse
import random
import subprocess
import sys


def random_id(prefix: str, n: int) -> str:
    return f"{prefix}{n:05d}"


def license_for_index(n: int) -> str:
    return f"{n:03d}-{n:03d}-{n:03d}"


def generate_sql(count: int) -> str:
    brands = ["Toyota", "Honda", "Ford", "BMW", "Audi", "Tesla", "Volvo"]
    types = ["Sedan", "SUV", "Hatchback", "Truck", "Coupe"]

    lines = []

    # Customer aspects live in their own per-service databases.
    lines.append("USE CustomerIdentity;")
    lines.append("DELETE FROM dbo.CustomerIdentity;")
    for i in range(count):
        cid = random_id("CUST", i)
        lines.append(
            f"INSERT INTO dbo.CustomerIdentity (CustomerId, Name) "
            f"VALUES ('{cid}', 'Performance Customer {i}');"
        )

    lines.append("USE CustomerAddress;")
    lines.append("DELETE FROM dbo.CustomerAddress;")
    for i in range(count):
        cid = random_id("CUST", i)
        lines.append(
            f"INSERT INTO dbo.CustomerAddress (CustomerId, Address, PostalCode, City) "
            f"VALUES ('{cid}', 'Street {i}', '{10000 + (i % 90000):05d}', 'Test City');"
        )

    lines.append("USE CustomerTelephone;")
    lines.append("DELETE FROM dbo.CustomerTelephone;")
    for i in range(count):
        cid = random_id("CUST", i)
        lines.append(
            f"INSERT INTO dbo.CustomerTelephone (CustomerId, TelephoneNumber) "
            f"VALUES ('{cid}', '555-{i:05d}');"
        )

    lines.append("USE CustomerEmail;")
    lines.append("DELETE FROM dbo.CustomerEmail;")
    for i in range(count):
        cid = random_id("CUST", i)
        lines.append(
            f"INSERT INTO dbo.CustomerEmail (CustomerId, EmailAddress) "
            f"VALUES ('{cid}', 'customer{i:05d}@pitstop.perf');"
        )

    # Vehicle aspects live in their own per-service databases.
    lines.append("USE VehicleInfo;")
    lines.append("DELETE FROM dbo.VehicleInfo;")
    for i in range(count):
        lid = license_for_index(i)
        brand = random.choice(brands)
        vtype = random.choice(types)
        lines.append(
            f"INSERT INTO dbo.VehicleInfo (LicenseNumber, Brand, Type) "
            f"VALUES ('{lid}', '{brand}', '{vtype}');"
        )

    lines.append("USE VehicleOwner;")
    lines.append("DELETE FROM dbo.VehicleOwner;")
    for i in range(count):
        lid = license_for_index(i)
        owner = random_id("CUST", i % count)
        lines.append(
            f"INSERT INTO dbo.VehicleOwner (LicenseNumber, OwnerId) "
            f"VALUES ('{lid}', '{owner}');"
        )

    # Also populate the WorkshopManagement read-model so the WorkshopManagement page works.
    lines.append("USE WorkshopManagement;")
    lines.append("DELETE FROM dbo.MaintenanceJob;")
    lines.append("DELETE FROM dbo.Vehicle;")
    lines.append("DELETE FROM dbo.Customer;")
    for i in range(count):
        cid = random_id("CUST", i)
        lines.append(
            f"INSERT INTO dbo.Customer (CustomerId, Name, TelephoneNumber) "
            f"VALUES ('{cid}', 'Performance Customer {i}', '555-{i:05d}');"
        )
    for i in range(count):
        lid = license_for_index(i)
        owner = random_id("CUST", i % count)
        brand = random.choice(brands)
        vtype = random.choice(types)
        lines.append(
            f"INSERT INTO dbo.Vehicle (LicenseNumber, Brand, Type, OwnerId) "
            f"VALUES ('{lid}', '{brand}', '{vtype}', '{owner}');"
        )

    return "\n".join(lines)


def run_sql(sql: str):
    cmd = [
        "docker", "exec", "-i", "sqlserver",
        "/opt/mssql-tools18/bin/sqlcmd",
        "-S", "localhost",
        "-U", "sa",
        "-P", "8jkGh47hnDw89H@q8LN2",
        "-C",
    ]
    result = subprocess.run(cmd, input=sql, text=True, capture_output=True)
    if result.returncode != 0:
        print("SQL seeding failed:", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
    print("SQL seeding completed successfully.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=500)
    args = parser.parse_args()

    print(f"Generating SQL seed data ({args.count} customers/vehicles)...")
    sql = generate_sql(args.count)
    print("Executing SQL seed...")
    run_sql(sql)
    print(f"Seeded {args.count} customers and {args.count} vehicles.")


if __name__ == "__main__":
    main()
