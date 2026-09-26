namespace Pitstop.CustomerManagementAPI.Mappers;

public static class Mappers
{
    public static CustomerRegistered MapToCustomerRegistered(this RegisterCustomer command) => new CustomerRegistered
    (
        System.Guid.NewGuid(),
        command.CustomerId
    );
}