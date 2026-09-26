using Dapper;
using Microsoft.Data.SqlClient;
using Serilog;

var builder = WebApplication.CreateBuilder(args);

builder.Host.UseSerilog((context, logContext) =>
    logContext.ReadFrom.Configuration(builder.Configuration)
);

var connectionString = builder.Configuration.GetConnectionString("CustomerAddressCN")!;

builder.Services.AddControllers()
    .AddJsonOptions(options => options.JsonSerializerOptions.PropertyNamingPolicy = null);

builder.Services.AddHealthChecks()
    .AddSqlServer(connectionString, name: "CustomerAddressDBHC");

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.UseDeveloperExceptionPage();
}

app.UseHealthChecks("/hc");
app.MapControllers();

await InitializeDatabaseAsync(app);

app.Run();

static async Task InitializeDatabaseAsync(WebApplication app)
{
    var cs = app.Configuration.GetConnectionString("CustomerAddressCN")!;
    const int maxAttempts = 10;
    for (var attempt = 1; attempt <= maxAttempts; attempt++)
    {
        try
        {
            await using (var connection = new SqlConnection(cs.Replace("CustomerAddress", "master")))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    "IF NOT EXISTS(SELECT * FROM master.sys.databases WHERE name='CustomerAddress') CREATE DATABASE CustomerAddress;");
            }
            await using (var connection = new SqlConnection(cs))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    @"IF OBJECT_ID('CustomerAddress') IS NULL
                      CREATE TABLE CustomerAddress (
                          CustomerId varchar(50) NOT NULL,
                          Address varchar(50),
                          PostalCode varchar(50),
                          City varchar(50),
                          PRIMARY KEY(CustomerId));");
            }
            return;
        }
        catch (Exception)
        {
            if (attempt == maxAttempts)
            {
                throw;
            }
            await Task.Delay(TimeSpan.FromSeconds(10));
        }
    }
}
