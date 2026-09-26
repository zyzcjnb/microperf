namespace Pitstop.CustomerManagementAPI.Model;

public class CustomerIdentity
{
    public string CustomerId { get; set; }
    public string Name { get; set; }
}

public class CustomerAddress
{
    public string CustomerId { get; set; }
    public string Address { get; set; }
    public string PostalCode { get; set; }
    public string City { get; set; }
}

public class CustomerTelephone
{
    public string CustomerId { get; set; }
    public string TelephoneNumber { get; set; }
}

public class CustomerEmail
{
    public string CustomerId { get; set; }
    public string EmailAddress { get; set; }
}
