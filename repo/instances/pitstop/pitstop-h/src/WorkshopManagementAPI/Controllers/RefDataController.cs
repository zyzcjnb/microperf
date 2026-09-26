namespace Pitstop.WorkshopManagementAPI.Controllers;

[Route("/api/[controller]")]
public class RefDataController : Controller
{
    private static readonly HttpClient _httpClient = new HttpClient();
    private readonly string _hubApi;

    public RefDataController(IConfiguration config)
    {
        _hubApi = config.GetValue<string>("HubApi") ?? "http://customermanagementapi:5100";
    }

    [HttpGet]
    [Route("customers")]
    public Task<IActionResult> GetCustomers() => ProxyGet($"{_hubApi}/api/hub/customers");

    [HttpGet]
    [Route("customers/{customerId}")]
    public Task<IActionResult> GetCustomerByCustomerId(string customerId) =>
        ProxyGet($"{_hubApi}/api/hub/customers/{customerId}");

    [HttpGet]
    [Route("vehicles")]
    public Task<IActionResult> GetVehicles() => ProxyGet($"{_hubApi}/api/hub/vehicles");

    [HttpGet]
    [Route("vehicles/{licenseNumber}")]
    public Task<IActionResult> GetVehicleByLicenseNumber(string licenseNumber) =>
        ProxyGet($"{_hubApi}/api/hub/vehicles/{licenseNumber}");

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
