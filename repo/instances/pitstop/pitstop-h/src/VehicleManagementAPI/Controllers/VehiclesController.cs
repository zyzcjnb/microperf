namespace Pitstop.VehicleManagement.Controllers;

[Route("/api/[controller]")]
public class VehiclesController : Controller
{
    private static readonly HttpClient _httpClient = new HttpClient();
    private readonly VehicleManagementDBContext _dbContext;
    private readonly string _hubApi;

    public VehiclesController(VehicleManagementDBContext dbContext, IConfiguration config)
    {
        _dbContext = dbContext;
        _hubApi = config.GetValue<string>("HubApi") ?? "http://customermanagementapi:5100";
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        return Ok(await _dbContext.Vehicles.ToListAsync());
    }

    [HttpGet]
    [Route("{licenseNumber}", Name = "GetByLicenseNumber")]
    public async Task<IActionResult> GetByLicenseNumber(string licenseNumber)
    {
        var vehicle = await _dbContext.Vehicles.FirstOrDefaultAsync(v => v.LicenseNumber == licenseNumber);
        if (vehicle == null)
        {
            return NotFound();
        }
        return Ok(vehicle);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync()
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, $"{_hubApi}/api/hub/vehicles");
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
