using Microsoft.AspNetCore.Mvc;
using Dapper;
using Pitstop.CustomerEmailService.Model;
using Microsoft.Data.SqlClient;

namespace Pitstop.CustomerEmailService.Controllers;

[Route("api/[controller]")]
[ApiController]
public class CustomersController : ControllerBase
{
    private readonly string _connectionString;

    public CustomersController(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("CustomerEmailCN")!;
    }

    [HttpGet]
    public async Task<IActionResult> GetAllAsync()
    {
        using var connection = new SqlConnection(_connectionString);
        var emails = await connection.QueryAsync<CustomerEmail>(
            "SELECT CustomerId, EmailAddress FROM CustomerEmail");
        return Ok(emails);
    }

    [HttpGet("{customerId}")]
    public async Task<IActionResult> GetByCustomerIdAsync(string customerId)
    {
        using var connection = new SqlConnection(_connectionString);
        var email = await connection.QueryFirstOrDefaultAsync<CustomerEmail>(
            "SELECT CustomerId, EmailAddress FROM CustomerEmail WHERE CustomerId = @customerId",
            new { customerId });
        if (email == null)
        {
            return NotFound();
        }
        return Ok(email);
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] CustomerEmail email)
    {
        using var connection = new SqlConnection(_connectionString);
        await connection.ExecuteAsync(
            "INSERT INTO CustomerEmail (CustomerId, EmailAddress) VALUES (@CustomerId, @EmailAddress)",
            email);
        return StatusCode(StatusCodes.Status201Created, email);
    }
}
