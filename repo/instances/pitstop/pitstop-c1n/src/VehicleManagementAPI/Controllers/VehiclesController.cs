using System.Net.Http.Json;

namespace Pitstop.VehicleManagement.Controllers;

[Route("/api/[controller]")]
public class VehiclesController : Controller
{
    private const string NUMBER_PATTERN = @"^((\d{1,3}|[a-z]{1,3})-){2}(\d{1,3}|[a-z]{1,3})$";
    private const string InfoServiceUrl = "http://vehicleinfoservice:5050";
    private const string OwnerServiceUrl = "http://vehicleownerservice:5051";

    private readonly IMessagePublisher _messagePublisher;
    private readonly IHttpClientFactory _httpClientFactory;

    public VehiclesController(IMessagePublisher messagePublisher, IHttpClientFactory httpClientFactory)
    {
        _messagePublisher = messagePublisher;
        _httpClientFactory = httpClientFactory;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        var client = _httpClientFactory.CreateClient();

        // fetch vehicle info first, then the owner data from its own service
        var infos = await client.GetFromJsonAsync<List<VehicleInfo>>($"{InfoServiceUrl}/api/vehicles");
        var owners = (await client.GetFromJsonAsync<List<VehicleOwner>>($"{OwnerServiceUrl}/api/vehicles"))!
            .ToDictionary(o => o.LicenseNumber);

        var vehicles = infos!.Select(i =>
        {
            owners.TryGetValue(i.LicenseNumber, out var owner);
            return new Vehicle
            {
                LicenseNumber = i.LicenseNumber,
                Brand = i.Brand,
                Type = i.Type,
                OwnerId = owner?.OwnerId
            };
        }).ToList();

        return Ok(vehicles);
    }

    [HttpGet]
    [Route("{licenseNumber}", Name = "GetByLicenseNumber")]
    public async Task<IActionResult> GetByLicenseNumber(string licenseNumber)
    {
        var client = _httpClientFactory.CreateClient();

        var info = await GetOrNull<VehicleInfo>(client, $"{InfoServiceUrl}/api/vehicles/{licenseNumber}");
        if (info == null)
        {
            return NotFound();
        }
        var owner = await GetOrNull<VehicleOwner>(client, $"{OwnerServiceUrl}/api/vehicles/{licenseNumber}");

        var vehicle = new Vehicle
        {
            LicenseNumber = info.LicenseNumber,
            Brand = info.Brand,
            Type = info.Type,
            OwnerId = owner?.OwnerId
        };
        return Ok(vehicle);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] RegisterVehicle command)
    {
        try
        {
            if (ModelState.IsValid)
            {
                // check invariants
                if (!Regex.IsMatch(command.LicenseNumber, NUMBER_PATTERN, RegexOptions.IgnoreCase))
                {
                    return BadRequest($"The specified license-number '{command.LicenseNumber}' was not in the correct format.");
                }

                var client = _httpClientFactory.CreateClient();

                // store vehicle info and owner data in their own services, one after another
                (await client.PostAsJsonAsync($"{InfoServiceUrl}/api/vehicles",
                    new VehicleInfo { LicenseNumber = command.LicenseNumber, Brand = command.Brand, Type = command.Type }))
                    .EnsureSuccessStatusCode();
                (await client.PostAsJsonAsync($"{OwnerServiceUrl}/api/vehicles",
                    new VehicleOwner { LicenseNumber = command.LicenseNumber, OwnerId = command.OwnerId }))
                    .EnsureSuccessStatusCode();

                // send event
                var e = VehicleRegistered.FromCommand(command);
                await _messagePublisher.PublishMessageAsync(e.MessageType, e, "");

                var vehicle = new Vehicle
                {
                    LicenseNumber = command.LicenseNumber,
                    Brand = command.Brand,
                    Type = command.Type,
                    OwnerId = command.OwnerId
                };

                //return result
                return CreatedAtRoute("GetByLicenseNumber", new { licenseNumber = vehicle.LicenseNumber }, vehicle);
            }
            return BadRequest();
        }
        catch (HttpRequestException)
        {
            ModelState.AddModelError("", "Unable to save changes. " +
                "Try again, and if the problem persists " +
                "see your system administrator.");
            return StatusCode(StatusCodes.Status500InternalServerError);
        }
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
