using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.VehicleOwnerService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.VehicleOwnerService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class VehiclesController : ControllerBase
{
    private readonly string _connectionString;

    public VehiclesController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("VehicleOwnerCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var vehicles = await connection.QueryAsync<VehicleOwner>(
            "SELECT LicenseNumber, OwnerId FROM VehicleOwner");
        return Ok(vehicles);
    }

    [HttpGet("{licenseNumber}")]
    public async Task<IActionResult> GetByLicenseNumberAsync(string licenseNumber)
    {
        using var connection = new SqlConnection(_connectionString);
        var vehicle = await connection.QueryFirstOrDefaultAsync<VehicleOwner>(
            "SELECT LicenseNumber, OwnerId FROM VehicleOwner WHERE LicenseNumber = @licenseNumber",
            new { licenseNumber });
        if (vehicle == null)
        {
            return NotFound();
        }
        return Ok(vehicle);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] VehicleOwner vehicle)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO VehicleOwner (LicenseNumber, OwnerId) VALUES (@LicenseNumber, @OwnerId)",
            vehicle);
        return StatusCode(StatusCodes.Status201Created, vehicle);
    }
}
