using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.CustomerTelephoneService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.CustomerTelephoneService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class CustomersController : ControllerBase
{
    private readonly string _connectionString;

    public CustomersController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("CustomerTelephoneCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var telephones = await connection.QueryAsync<CustomerTelephone>(
            "SELECT CustomerId, TelephoneNumber FROM CustomerTelephone");
        return Ok(telephones);
    }

    [HttpGet("{customerId}")]
    public async Task<IActionResult> GetByCustomerIdAsync(string customerId)
    {
        using var connection = new SqlConnection(_connectionString);
        var telephone = await connection.QueryFirstOrDefaultAsync<CustomerTelephone>(
            "SELECT CustomerId, TelephoneNumber FROM CustomerTelephone WHERE CustomerId = @customerId",
            new { customerId });
        if (telephone == null)
        {
            return NotFound();
        }
        return Ok(telephone);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] CustomerTelephone telephone)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO CustomerTelephone (CustomerId, TelephoneNumber) VALUES (@CustomerId, @TelephoneNumber)",
            telephone);
        return StatusCode(StatusCodes.Status201Created, telephone);
    }
}
