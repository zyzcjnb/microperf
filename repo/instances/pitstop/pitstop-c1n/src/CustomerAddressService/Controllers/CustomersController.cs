using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.CustomerAddressService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.CustomerAddressService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class CustomersController : ControllerBase
{
    private readonly string _connectionString;

    public CustomersController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("CustomerAddressCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var addresses = await connection.QueryAsync<CustomerAddress>(
            "SELECT CustomerId, Address, PostalCode, City FROM CustomerAddress");
        return Ok(addresses);
    }

    [HttpGet("{customerId}")]
    public async Task<IActionResult> GetByCustomerIdAsync(string customerId)
    {
        using var connection = new SqlConnection(_connectionString);
        var address = await connection.QueryFirstOrDefaultAsync<CustomerAddress>(
            "SELECT CustomerId, Address, PostalCode, City FROM CustomerAddress WHERE CustomerId = @customerId",
            new { customerId });
        if (address == null)
        {
            return NotFound();
        }
        return Ok(address);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] CustomerAddress address)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO CustomerAddress (CustomerId, Address, PostalCode, City) VALUES (@CustomerId, @Address, @PostalCode, @City)",
            address);
        return StatusCode(StatusCodes.Status201Created, address);
    }
}
