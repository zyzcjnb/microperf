using Dapper;
using Microsoft.Data.SqlClient;
using Serilog;

var builder = WebApplication.CreateBuilder(args);

builder.Host.UseSerilog((context, logContext) =>
    logContext.ReadFrom.Configuration(builder.Configuration)
);

var connectionString = builder.Configuration.GetConnectionString("VehicleInfoCN")!;

builder.Services.AddControllers()
    .AddJsonOptions(options => options.JsonSerializerOptions.PropertyNamingPolicy = null);

builder.Services.AddHealthChecks()
    .AddSqlServer(connectionString, name: "VehicleInfoDBHC");

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
    var cs = app.Configuration.GetConnectionString("VehicleInfoCN")!;
    const int maxAttempts = 10;
    for (var attempt = 1; attempt <= maxAttempts; attempt++)
    {
        try
        {
            await using (var connection = new SqlConnection(cs.Replace("VehicleInfo", "master")))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    "IF NOT EXISTS(SELECT * FROM master.sys.databases WHERE name='VehicleInfo') CREATE DATABASE VehicleInfo;");
            }
            await using (var connection = new SqlConnection(cs))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    @"IF OBJECT_ID('VehicleInfo') IS NULL
                      CREATE TABLE VehicleInfo (
                          LicenseNumber varchar(50) NOT NULL,
                          Brand varchar(50),
                          Type varchar(50),
                          PRIMARY KEY(LicenseNumber));");
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
