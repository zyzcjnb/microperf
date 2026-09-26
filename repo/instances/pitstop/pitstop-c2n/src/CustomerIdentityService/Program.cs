using Dapper;
using Microsoft.Data.SqlClient;
using Serilog;

var builder = WebApplication.CreateBuilder(args);

builder.Host.UseSerilog((context, logContext) =>
    logContext.ReadFrom.Configuration(builder.Configuration)
);

var connectionString = builder.Configuration.GetConnectionString("CustomerIdentityCN")!;

builder.Services.AddControllers()
    .AddJsonOptions(options => options.JsonSerializerOptions.PropertyNamingPolicy = null);

builder.Services.AddHealthChecks()
    .AddSqlServer(connectionString, name: "CustomerIdentityDBHC");

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
    var cs = app.Configuration.GetConnectionString("CustomerIdentityCN")!;
    const int maxAttempts = 10;
    for (var attempt = 1; attempt <= maxAttempts; attempt++)
    {
        try
        {
            await using (var connection = new SqlConnection(cs.Replace("CustomerIdentity", "master")))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    "IF NOT EXISTS(SELECT * FROM master.sys.databases WHERE name='CustomerIdentity') CREATE DATABASE CustomerIdentity;");
            }
            await using (var connection = new SqlConnection(cs))
            {
                await connection.OpenAsync();
                await connection.ExecuteAsync(
                    @"IF OBJECT_ID('CustomerIdentity') IS NULL
                      CREATE TABLE CustomerIdentity (
                          CustomerId varchar(50) NOT NULL,
                          Name varchar(50) NOT NULL,
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
