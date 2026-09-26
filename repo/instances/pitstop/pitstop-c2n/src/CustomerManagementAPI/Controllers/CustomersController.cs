using System.Net.Http.Json;

namespace Pitstop.Application.CustomerManagementAPI.Controllers;

[Route("/api/[controller]")]
public class CustomersController : Controller
{
    private const string IdentityServiceUrl = "http://customeridentityservice:5150";
    private const string AddressServiceUrl = "http://customeraddressservice:5151";
    private const string TelephoneServiceUrl = "http://customertelephoneservice:5152";
    private const string EmailServiceUrl = "http://customeremailservice:5153";

    private readonly IMessagePublisher _messagePublisher;
    private readonly IHttpClientFactory _httpClientFactory;

    public CustomersController(IMessagePublisher messagePublisher, IHttpClientFactory httpClientFactory)
    {
        _messagePublisher = messagePublisher;
        _httpClientFactory = httpClientFactory;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        var client = _httpClientFactory.CreateClient();

        // fetch the identity list once
        var identities = await client.GetFromJsonAsync<List<CustomerIdentity>>($"{IdentityServiceUrl}/api/customers");

        // then fetch each customer's remaining aspects from their services one by one
        var customers = new List<Customer>();
        foreach (var identity in identities!)
        {
            var address = await GetOrNull<CustomerAddress>(client, $"{AddressServiceUrl}/api/customers/{identity.CustomerId}");
            var telephone = await GetOrNull<CustomerTelephone>(client, $"{TelephoneServiceUrl}/api/customers/{identity.CustomerId}");
            var email = await GetOrNull<CustomerEmail>(client, $"{EmailServiceUrl}/api/customers/{identity.CustomerId}");

            customers.Add(new Customer
            {
                CustomerId = identity.CustomerId,
                Name = identity.Name,
                Address = address?.Address,
                PostalCode = address?.PostalCode,
                City = address?.City,
                TelephoneNumber = telephone?.TelephoneNumber,
                EmailAddress = email?.EmailAddress
            });
        }

        return Ok(customers);
    }

    [HttpGet]
    [Route("{customerId}", Name = "GetByCustomerId")]
    public async Task<IActionResult> GetByCustomerId(string customerId)
    {
        var client = _httpClientFactory.CreateClient();

        var identity = await GetOrNull<CustomerIdentity>(client, $"{IdentityServiceUrl}/api/customers/{customerId}");
        if (identity == null)
        {
            return NotFound();
        }
        var address = await GetOrNull<CustomerAddress>(client, $"{AddressServiceUrl}/api/customers/{customerId}");
        var telephone = await GetOrNull<CustomerTelephone>(client, $"{TelephoneServiceUrl}/api/customers/{customerId}");
        var email = await GetOrNull<CustomerEmail>(client, $"{EmailServiceUrl}/api/customers/{customerId}");

        var customer = new Customer
        {
            CustomerId = identity.CustomerId,
            Name = identity.Name,
            Address = address?.Address,
            PostalCode = address?.PostalCode,
            City = address?.City,
            TelephoneNumber = telephone?.TelephoneNumber,
            EmailAddress = email?.EmailAddress
        };
        return Ok(customer);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] RegisterCustomer command)
    {
        if (!ModelState.IsValid)
        {
            return BadRequest();
        }

        var client = _httpClientFactory.CreateClient();

        // store every aspect of the customer in its own service, one after another
        (await client.PostAsJsonAsync($"{IdentityServiceUrl}/api/customers",
            new CustomerIdentity { CustomerId = command.CustomerId, Name = command.Name }))
            .EnsureSuccessStatusCode();
        (await client.PostAsJsonAsync($"{AddressServiceUrl}/api/customers",
            new CustomerAddress
            {
                CustomerId = command.CustomerId,
                Address = command.Address,
                PostalCode = command.PostalCode,
                City = command.City
            }))
            .EnsureSuccessStatusCode();
        (await client.PostAsJsonAsync($"{TelephoneServiceUrl}/api/customers",
            new CustomerTelephone { CustomerId = command.CustomerId, TelephoneNumber = command.TelephoneNumber }))
            .EnsureSuccessStatusCode();
        (await client.PostAsJsonAsync($"{EmailServiceUrl}/api/customers",
            new CustomerEmail { CustomerId = command.CustomerId, EmailAddress = command.EmailAddress }))
            .EnsureSuccessStatusCode();

        // send event
        CustomerRegistered e = command.MapToCustomerRegistered();
        await _messagePublisher.PublishMessageAsync(e.MessageType, e, "");

        var customer = new Customer
        {
            CustomerId = command.CustomerId,
            Name = command.Name,
            Address = command.Address,
            PostalCode = command.PostalCode,
            City = command.City,
            TelephoneNumber = command.TelephoneNumber,
            EmailAddress = command.EmailAddress
        };

        // return result
        return CreatedAtRoute("GetByCustomerId", new { customerId = customer.CustomerId }, customer);
    }

    private static async Task<T?> GetOrNull<T>(HttpClient client, string url) where T : class
    {
        var response = await client.GetAsync(url);
        if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
        {
            return null;
        }
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<T>();
    }
}
