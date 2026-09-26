using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.CustomerIdentityService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.CustomerIdentityService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class CustomersController : ControllerBase
{
    private readonly string _connectionString;

    public CustomersController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("CustomerIdentityCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var customers = await connection.QueryAsync<CustomerIdentity>(
            "SELECT CustomerId, Name FROM CustomerIdentity");
        return Ok(customers);
    }

    [HttpGet("{customerId}")]
    public async Task<IActionResult> GetByCustomerIdAsync(string customerId)
    {
        using var connection = new SqlConnection(_connectionString);
        var customer = await connection.QueryFirstOrDefaultAsync<CustomerIdentity>(
            "SELECT CustomerId, Name FROM CustomerIdentity WHERE CustomerId = @customerId",
            new { customerId });
        if (customer == null)
        {
            return NotFound();
        }
        return Ok(customer);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] CustomerIdentity customer)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO CustomerIdentity (CustomerId, Name) VALUES (@CustomerId, @Name)",
            customer);
        return StatusCode(StatusCodes.Status201Created, customer);
    }
}
