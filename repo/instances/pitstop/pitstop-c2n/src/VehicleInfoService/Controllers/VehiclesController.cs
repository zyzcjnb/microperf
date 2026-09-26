using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.VehicleInfoService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.VehicleInfoService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class VehiclesController : ControllerBase
{
    private readonly string _connectionString;

    public VehiclesController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("VehicleInfoCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var vehicles = await connection.QueryAsync<VehicleInfo>(
            "SELECT LicenseNumber, Brand, Type FROM VehicleInfo");
        return Ok(vehicles);
    }

    [HttpGet("{licenseNumber}")]
    public async Task<IActionResult> GetByLicenseNumberAsync(string licenseNumber)
    {
        using var connection = new SqlConnection(_connectionString);
        var vehicle = await connection.QueryFirstOrDefaultAsync<VehicleInfo>(
            "SELECT LicenseNumber, Brand, Type FROM VehicleInfo WHERE LicenseNumber = @licenseNumber",
            new { licenseNumber });
        if (vehicle == null)
        {
            return NotFound();
        }
        return Ok(vehicle);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] VehicleInfo vehicle)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO VehicleInfo (LicenseNumber, Brand, Type) VALUES (@LicenseNumber, @Brand, @Type)",
            vehicle);
        return StatusCode(StatusCodes.Status201Created, vehicle);
    }
}
