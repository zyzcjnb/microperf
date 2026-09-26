namespace Pitstop.WorkshopManagementAPI.Controllers;

[Route("/api/[controller]")]
public class RefDataController : Controller
{
    private static readonly HttpClient _httpClient = new HttpClient();
    private readonly string _customerApi;
    private readonly string _vehicleApi;

    public RefDataController(IConfiguration config)
    {
        _customerApi = config.GetValue<string>("ApiEndpoints:CustomerManagementApi") ?? "http://customermanagementapi:5100";
        _vehicleApi = config.GetValue<string>("ApiEndpoints:VehicleManagementApi") ?? "http://vehiclemanagementapi:5000";
    }

    [HttpGet]
    [Route("customers")]
    public Task<IActionResult> GetCustomers() =>
        ProxyGet($"{_customerApi}/api/customers");

    [HttpGet]
    [Route("customers/{customerId}")]
    public Task<IActionResult> GetCustomerByCustomerId(string customerId) =>
        ProxyGet($"{_customerApi}/api/customers/{customerId}");

    [HttpGet]
    [Route("vehicles")]
    public Task<IActionResult> GetVehicles() =>
        ProxyGet($"{_vehicleApi}/api/vehicles");

    [HttpGet]
    [Route("vehicles/{licenseNumber}")]
    public Task<IActionResult> GetVehicleByLicenseNumber(string licenseNumber) =>
        ProxyGet($"{_vehicleApi}/api/vehicles/{licenseNumber}");

    private async Task<IActionResult> ProxyGet(string url)
    {
        var response = await _httpClient.GetAsync(url);
        var content = await response.Content.ReadAsStringAsync();
        return new ContentResult
        {
            Content = content,
            ContentType = "application/json",
            StatusCode = (int)response.StatusCode
        };
    }
}
