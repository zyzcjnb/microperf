namespace Pitstop.CustomerManagementAPI.Controllers;

[Route("/api/[controller]")]
public class HubController : Controller
{
    private static readonly HttpClient _httpClient = new HttpClient();

    private readonly CustomerManagementDBContext _dbContext;
    private readonly string _vehicleApi;
    private readonly string _workshopApi;

    public HubController(CustomerManagementDBContext dbContext, IConfiguration config)
    {
        _dbContext = dbContext;
        _vehicleApi = config.GetValue<string>("HubDependencies:VehicleManagementApi")
            ?? "http://vehiclemanagementapi:5000";
        _workshopApi = config.GetValue<string>("HubDependencies:WorkshopManagementApi")
            ?? "http://workshopmanagementapi:5200";
    }

    [HttpGet]
    [Route("customers")]
    public async Task<IActionResult> GetCustomers()
    {
        var customers = await _dbContext.Customers.ToListAsync();
        var vehicles = await GetVehiclesFromVehicleServiceAsync();
        var lookup = vehicles
            .GroupBy(v => v.Value<string>("ownerId"))
            .ToDictionary(g => g.Key, g => g.ToList());

        var result = customers.Select(c => new
        {
            c.CustomerId,
            c.Name,
            c.Address,
            c.PostalCode,
            c.City,
            c.TelephoneNumber,
            c.EmailAddress,
            Vehicles = lookup.TryGetValue(c.CustomerId, out var v) ? v : new List<JObject>()
        });
        return Ok(result);
    }

    [HttpGet]
    [Route("customers/{customerId}")]
    public async Task<IActionResult> GetCustomerById(string customerId)
    {
        var customer = await _dbContext.Customers.FirstOrDefaultAsync(c => c.CustomerId == customerId);
        if (customer == null)
        {
            return NotFound();
        }
        var vehicles = (await GetVehiclesFromVehicleServiceAsync())
            .Where(v => v.Value<string>("ownerId") == customerId)
            .ToList();
        return Ok(new
        {
            customer.CustomerId,
            customer.Name,
            customer.Address,
            customer.PostalCode,
            customer.City,
            customer.TelephoneNumber,
            customer.EmailAddress,
            Vehicles = vehicles
        });
    }

    [HttpGet]
    [Route("vehicles")]
    public async Task<IActionResult> GetVehicles()
    {
        var vehicles = await GetVehiclesFromVehicleServiceAsync();
        var customers = await _dbContext.Customers.ToListAsync();
        var byId = customers.ToDictionary(c => c.CustomerId);
        var result = vehicles.Select(v =>
        {
            byId.TryGetValue(v.Value<string>("ownerId") ?? "", out var owner);
            return new
            {
                LicenseNumber = v.Value<string>("licenseNumber"),
                Brand = v.Value<string>("brand"),
                Type = v.Value<string>("type"),
                OwnerId = v.Value<string>("ownerId"),
                OwnerName = owner?.Name
            };
        });
        return Ok(result);
    }

    [HttpGet]
    [Route("vehicles/{licenseNumber}")]
    public async Task<IActionResult> GetVehicleByLicenseNumber(string licenseNumber)
    {
        var response = await _httpClient.GetAsync($"{_vehicleApi}/api/vehicles/{licenseNumber}");
        if (!response.IsSuccessStatusCode)
        {
            return NotFound();
        }
        var vehicle = JObject.Parse(await response.Content.ReadAsStringAsync());
        var owner = await _dbContext.Customers
            .FirstOrDefaultAsync(c => c.CustomerId == vehicle.Value<string>("ownerId"));
        return Ok(new
        {
            LicenseNumber = vehicle.Value<string>("licenseNumber"),
            Brand = vehicle.Value<string>("brand"),
            Type = vehicle.Value<string>("type"),
            OwnerId = vehicle.Value<string>("ownerId"),
            OwnerName = owner?.Name
        });
    }

    private async Task<List<JObject>> GetVehiclesFromVehicleServiceAsync()
    {
        var response = await _httpClient.GetAsync($"{_vehicleApi}/api/vehicles");
        response.EnsureSuccessStatusCode();
        var content = await response.Content.ReadAsStringAsync();
        return JArray.Parse(content).Cast<JObject>().ToList();
    }

    [HttpPost]
    [Route("vehicles")]
    public Task<IActionResult> RegisterVehicle() =>
        ProxySendAsync(HttpMethod.Post, $"{_vehicleApi}/api/internal/vehicles");

    [HttpPost]
    [Route("workshopplanning/{planningDate}/jobs")]
    public Task<IActionResult> PlanMaintenanceJob(string planningDate) =>
        ProxySendAsync(HttpMethod.Post, $"{_workshopApi}/api/internal/workshopplanning/{planningDate}/jobs");

    [HttpPut]
    [Route("workshopplanning/{planningDate}/jobs/{jobId}/finish")]
    public Task<IActionResult> FinishMaintenanceJob(string planningDate, string jobId) =>
        ProxySendAsync(HttpMethod.Put, $"{_workshopApi}/api/internal/workshopplanning/{planningDate}/jobs/{jobId}/finish");

    private async Task<IActionResult> ProxySendAsync(HttpMethod method, string url)
    {
        using var request = new HttpRequestMessage(method, url);
        request.Content = new StreamContent(Request.Body);
        if (Request.ContentType != null)
        {
            request.Content.Headers.TryAddWithoutValidation("Content-Type", Request.ContentType);
        }
        var response = await _httpClient.SendAsync(request);
        var content = await response.Content.ReadAsStringAsync();
        return new ContentResult
        {
            Content = content,
            ContentType = "application/json",
            StatusCode = (int)response.StatusCode
        };
    }
}
