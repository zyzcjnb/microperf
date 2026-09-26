namespace Pitstop.WorkshopManagementEventHandler;

public class EventHandlerWorker : IHostedService, IMessageHandlerCallback
{
    WorkshopManagementDBContext _dbContext;
    IMessageHandler _messageHandler;
    IMessagePublisher _messagePublisher;
    HttpClient _httpClient;
    ApiEndpoints _endpoints;

    public EventHandlerWorker(IMessageHandler messageHandler, WorkshopManagementDBContext dbContext,
        IOptions<ApiEndpoints> endpoints, IMessagePublisher messagePublisher)
    {
        _messageHandler = messageHandler;
        _dbContext = dbContext;
        _messagePublisher = messagePublisher;
        _httpClient = new HttpClient();
        _endpoints = endpoints.Value;
    }

    public void Start()
    {
        _messageHandler.Start(this);
    }

    public void Stop()
    {
        _messageHandler.Stop();
    }

    public Task StartAsync(CancellationToken cancellationToken)
    {
        _messageHandler.Start(this);
        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken)
    {
        _messageHandler.Stop();
        return Task.CompletedTask;
    }

    private async Task AckAsync(Event source)
    {
        var ack = new EventAck(Guid.NewGuid(), source.MessageId, source.MessageType);
        await _messagePublisher.PublishMessageAsync(ack.MessageType, ack, "");
    }

    public async Task<bool> HandleMessageAsync(string messageType, string message)
    {
        JObject messageObject = MessageSerializer.Deserialize(message);
        try
        {
            switch (messageType)
            {
                case "CustomerRegistered":
                    await HandleAsync(messageObject.ToObject<CustomerRegistered>());
                    break;
                case "VehicleRegistered":
                    await HandleAsync(messageObject.ToObject<VehicleRegistered>());
                    break;
                case "MaintenanceJobPlanned":
                    await HandleAsync(messageObject.ToObject<MaintenanceJobPlanned>());
                    break;
                case "MaintenanceJobFinished":
                    await HandleAsync(messageObject.ToObject<MaintenanceJobFinished>());
                    break;
            }
        }
        catch (Exception ex)
        {
            string messageId = messageObject.Property("MessageId") != null ? messageObject.Property("MessageId").Value<string>() : "[unknown]";
            Log.Error(ex, "Error while handling {MessageType} message with id {MessageId}.", messageType, messageId);
        }

        // always akcnowledge message - any errors need to be dealt with locally.
        return true;
    }

    private async Task<JObject> GetAsync(string url)
    {
        int attempts = 0;
        while (true)
        {
            var response = await _httpClient.GetAsync(url);
            if (response.IsSuccessStatusCode)
            {
                var content = await response.Content.ReadAsStringAsync();
                return JObject.Parse(content);
            }

            if (++attempts >= 3)
            {
                response.EnsureSuccessStatusCode();
            }

            await Task.Delay(50);
        }
    }

    private async Task<bool> HandleAsync(VehicleRegistered e)
    {
        var vehicleData = await GetAsync($"{_endpoints.VehicleManagementApi}/api/vehicles/{e.LicenseNumber}");
        string brand = vehicleData.Value<string>("brand");
        string type = vehicleData.Value<string>("type");
        string ownerId = vehicleData.Value<string>("ownerId");

        Log.Information("Register Vehicle: {LicenseNumber}, {Brand}, {Type}, Owner Id: {OwnerId}",
            e.LicenseNumber, brand, type, ownerId);

        try
        {
            await _dbContext.Vehicles.AddAsync(new Vehicle
            {
                LicenseNumber = e.LicenseNumber,
                Brand = brand,
                Type = type,
                OwnerId = ownerId
            });
            await _dbContext.SaveChangesAsync();
        }
        catch (DbUpdateException)
        {
            Console.WriteLine($"Skipped adding vehicle with license number {e.LicenseNumber}.");
        }

        await AckAsync(e);
        return true;
    }

    private async Task<bool> HandleAsync(CustomerRegistered e)
    {
        var customerData = await GetAsync($"{_endpoints.CustomerManagementApi}/api/customers/{e.CustomerId}");
        string name = customerData.Value<string>("name");
        string telephoneNumber = customerData.Value<string>("telephoneNumber");

        Log.Information("Register Customer: {CustomerId}, {Name}, {TelephoneNumber}",
            e.CustomerId, name, telephoneNumber);

        try
        {
            await _dbContext.Customers.AddAsync(new Customer
            {
                CustomerId = e.CustomerId,
                Name = name,
                TelephoneNumber = telephoneNumber
            });
            await _dbContext.SaveChangesAsync();
        }
        catch (DbUpdateException)
        {
            Log.Warning("Skipped adding customer with customer id {CustomerId}.", e.CustomerId);
        }

        await AckAsync(e);
        return true;
    }

    private async Task<bool> HandleAsync(MaintenanceJobPlanned e)
    {
        string dateString = e.PlanningDate.ToString("yyyy-MM-dd");
        var jobData = await GetAsync($"{_endpoints.WorkshopManagementApi}/api/workshopplanning/{dateString}/jobs/{e.JobId}");
        var startTime = jobData.Value<DateTime>("startTime");
        var endTime = jobData.Value<DateTime>("endTime");
        var description = jobData.Value<string>("description");
        var customerData = jobData["customer"];
        var vehicleData = jobData["vehicle"];

        string customerId = customerData.Value<string>("customerId");
        string customerName = customerData.Value<string>("name");
        string telephoneNumber = customerData.Value<string>("telephoneNumber");
        string licenseNumber = vehicleData.Value<string>("licenseNumber");
        string brand = vehicleData.Value<string>("brand");
        string type = vehicleData.Value<string>("type");

        Log.Information("Register Maintenance Job: {JobId}, {StartTime}, {EndTime}, {CustomerName}, {LicenseNumber}",
            e.JobId, startTime, endTime, customerName, licenseNumber);

        try
        {
            // determine customer
            Customer customer = await _dbContext.Customers.FirstOrDefaultAsync(c => c.CustomerId == customerId);
            if (customer == null)
            {
                customer = new Customer
                {
                    CustomerId = customerId,
                    Name = customerName,
                    TelephoneNumber = telephoneNumber
                };
            }

            // determine vehicle
            Vehicle vehicle = await _dbContext.Vehicles.FirstOrDefaultAsync(v => v.LicenseNumber == licenseNumber);
            if (vehicle == null)
            {
                vehicle = new Vehicle
                {
                    LicenseNumber = licenseNumber,
                    Brand = brand,
                    Type = type,
                    OwnerId = customerId
                };
            }

            // insert maintetancejob
            await _dbContext.MaintenanceJobs.AddAsync(new MaintenanceJob
            {
                Id = e.JobId,
                StartTime = startTime,
                EndTime = endTime,
                Customer = customer,
                Vehicle = vehicle,
                WorkshopPlanningDate = startTime.Date,
                Description = description
            });
            await _dbContext.SaveChangesAsync();
        }
        catch (DbUpdateException)
        {
            Log.Warning("Skipped adding maintenance job with id {JobId}.", e.JobId);
        }

        await AckAsync(e);
        return true;
    }

    private async Task<bool> HandleAsync(MaintenanceJobFinished e)
    {
        string dateString = e.PlanningDate.ToString("yyyy-MM-dd");
        var jobData = await GetAsync($"{_endpoints.WorkshopManagementApi}/api/workshopplanning/{dateString}/jobs/{e.JobId}");
        var actualStartTime = jobData.Value<DateTime?>("actualStartTime");
        var actualEndTime = jobData.Value<DateTime?>("actualEndTime");
        var notes = jobData.Value<string>("notes");

        Log.Information("Finish Maintenance job: {JobId}, {ActualStartTime}, {ActualEndTime}",
            e.JobId, actualStartTime, actualEndTime);

        try
        {
            // insert maintetancejob
            var job = await _dbContext.MaintenanceJobs.FirstOrDefaultAsync(j => j.Id == e.JobId);
            if (job != null)
            {
                job.ActualStartTime = actualStartTime;
                job.ActualEndTime = actualEndTime;
                job.Notes = notes;
                await _dbContext.SaveChangesAsync();
            }
        }
        catch (DbUpdateException)
        {
            Log.Warning("Skipped adding maintenance job with id {JobId}.", e.JobId);
        }

        await AckAsync(e);
        return true;
    }
}
