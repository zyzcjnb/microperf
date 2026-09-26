using System.Net.Http.Json;

namespace Pitstop.WorkshopManagementAPI.Controllers;

[Route("/api/[controller]")]
public class RefDataController : Controller
{
    ICustomerRepository _customerRepo;
    IVehicleRepository _vehicleRepo;
    IHttpClientFactory _httpClientFactory;

    public RefDataController(ICustomerRepository customerRepo, IVehicleRepository vehicleRepo, IHttpClientFactory httpClientFactory)
    {
        _customerRepo = customerRepo;
        _vehicleRepo = vehicleRepo;
        _httpClientFactory = httpClientFactory;
    }

    [HttpGet]
    [Route("customers")]
    public async Task<IActionResult> GetCustomers()
    {
        var client = _httpClientFactory.CreateClient();
        var response = await client.GetAsync("http://customermanagementapi:5100/api/customers");
        response.EnsureSuccessStatusCode();
        var customers = await response.Content.ReadFromJsonAsync<List<Repositories.Model.Customer>>();
        return Ok(customers);
    }

    [HttpGet]
    [Route("customers/{customerId}")]
    public async Task<IActionResult> GetCustomerByCustomerId(string customerId)
    {
        var customer = await _customerRepo.GetCustomerAsync(customerId);
        if (customer == null)
        {
            return NotFound();
        }
        return Ok(customer);
    }

    [HttpGet]
    [Route("vehicles")]
    public async Task<IActionResult> GetVehicles()
    {
        var client = _httpClientFactory.CreateClient();
        var response = await client.GetAsync("http://vehiclemanagementapi:5000/api/vehicles");
        response.EnsureSuccessStatusCode();
        var vehicles = await response.Content.ReadFromJsonAsync<List<Repositories.Model.Vehicle>>();
        return Ok(vehicles);
    }

    [HttpGet]
    [Route("vehicles/{licenseNumber}")]
    public async Task<IActionResult> GetVehicleByLicenseNumber(string licenseNumber)
    {
        var vehicle = await _vehicleRepo.GetVehicleAsync(licenseNumber);
        if (vehicle == null)
        {
            return NotFound();
        }
        return Ok(vehicle);
    }
}
