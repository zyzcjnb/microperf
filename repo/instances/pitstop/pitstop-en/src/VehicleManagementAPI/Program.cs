var builder = WebApplication.CreateBuilder(args);

// setup logging
builder.Host.UseSerilog((context, logContext) => 
    logContext
        .ReadFrom.Configuration(builder.Configuration)
        .Enrich.WithMachineName()
);

// add HttpClient for delegating vehicle storage to the downstream services
builder.Services.AddHttpClient();

// add messagepublisher
builder.Services.UseRabbitMQMessagePublisher(builder.Configuration);

builder.Services.AddSingleton<Pitstop.Services.AckAwaiter>();
builder.Services.AddHostedService<Pitstop.Services.AckListenerWorker>();
builder.Services.UseRabbitMQMessageHandler(builder.Configuration);

// Add framework services
builder.Services
    .AddMvc(options => options.EnableEndpointRouting = false)
    .AddNewtonsoftJson();

// Register the Swagger generator, defining one or more Swagger documents
builder.Services.AddSwaggerGen(c =>
{
    c.SwaggerDoc("v1", new OpenApiInfo { Title = "VehicleManagement API", Version = "v1" });
});

// Add health checks
builder.Services.AddHealthChecks();

// Setup MVC
builder.Services.AddControllers();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.UseDeveloperExceptionPage();
}

app.UseMvc();
app.UseDefaultFiles();
app.UseStaticFiles();

// Enable middleware to serve generated Swagger as a JSON endpoint.
app.UseSwagger();

// Enable middleware to serve swagger-ui (HTML, JS, CSS etc.), specifying the Swagger JSON endpoint.
app.UseSwaggerUI(c =>
{
    c.SwaggerEndpoint("/swagger/v1/swagger.json", "VehicleManagement API - v1");
});

app.UseHealthChecks("/hc");

app.MapControllers();

app.Run();
