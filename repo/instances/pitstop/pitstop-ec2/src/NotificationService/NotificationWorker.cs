namespace Pitstop.NotificationService;

public class NotificationWorker : IHostedService, IMessageHandlerCallback
{
    IMessageHandler _messageHandler;
    INotificationRepository _repo;
    IEmailNotifier _emailNotifier;
    HttpClient _httpClient;
    ApiEndpoints _endpoints;
    IMessagePublisher _messagePublisher;

    public NotificationWorker(IMessageHandler messageHandler, INotificationRepository repo, IEmailNotifier emailNotifier,
        IOptions<ApiEndpoints> endpoints, IMessagePublisher messagePublisher)
    {
        _messageHandler = messageHandler;
        _repo = repo;
        _emailNotifier = emailNotifier;
        _httpClient = new HttpClient();
        _endpoints = endpoints.Value;
        _messagePublisher = messagePublisher;
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
        try
        {
            JObject messageObject = MessageSerializer.Deserialize(message);
            switch (messageType)
            {
                case "CustomerRegistered":
                    await HandleAsync(messageObject.ToObject<CustomerRegistered>());
                    break;
                case "MaintenanceJobPlanned":
                    await HandleAsync(messageObject.ToObject<MaintenanceJobPlanned>());
                    break;
                case "MaintenanceJobFinished":
                    await HandleAsync(messageObject.ToObject<MaintenanceJobFinished>());
                    break;
                case "DayHasPassed":
                    await HandleAsync(messageObject.ToObject<DayHasPassed>());
                    break;
                default:
                    break;
            }
        }
        catch (Exception ex)
        {
            Log.Error(ex, $"Error while handling {messageType} event.");
        }

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

    private async Task HandleAsync(CustomerRegistered cr)
    {
        var customerData = await GetAsync($"{_endpoints.CustomerManagementApi}/api/customers/{cr.CustomerId}");

        Customer customer = new Customer
        {
            CustomerId = cr.CustomerId,
            Name = customerData.Value<string>("name"),
            TelephoneNumber = customerData.Value<string>("telephoneNumber"),
            EmailAddress = customerData.Value<string>("emailAddress")
        };

        Log.Information("Register customer: {Id}, {Name}, {TelephoneNumber}, {Email}",
            customer.CustomerId, customer.Name, customer.TelephoneNumber, customer.EmailAddress);

        await _repo.RegisterCustomerAsync(customer);
        await AckAsync(cr);
    }

    private async Task HandleAsync(MaintenanceJobPlanned mjp)
    {
        string dateString = mjp.PlanningDate.ToString("yyyy-MM-dd");
        var jobData = await GetAsync($"{_endpoints.WorkshopManagementApi}/api/workshopplanning/{dateString}/jobs/{mjp.JobId}");

        MaintenanceJob job = new MaintenanceJob
        {
            JobId = mjp.JobId.ToString(),
            CustomerId = jobData["customer"].Value<string>("customerId"),
            LicenseNumber = jobData["vehicle"].Value<string>("licenseNumber"),
            StartTime = jobData.Value<DateTime>("startTime"),
            Description = jobData.Value<string>("description")
        };

        Log.Information("Register Maintenance Job: {Id}, {CustomerId}, {VehicleLicenseNumber}, {StartTime}, {Description}",
            job.JobId, job.CustomerId, job.LicenseNumber, job.StartTime, job.Description);

        await _repo.RegisterMaintenanceJobAsync(job);
        await AckAsync(mjp);
    }

    private async Task HandleAsync(MaintenanceJobFinished mjf)
    {
        Log.Information("Remove finished Maintenance Job: {Id}", mjf.JobId);

        await _repo.RemoveMaintenanceJobsAsync(new string[] { mjf.JobId.ToString() });
    }

    private async Task HandleAsync(DayHasPassed dhp)
    {
        DateTime today = DateTime.Now;

        IEnumerable<MaintenanceJob> jobsToNotify = await _repo.GetMaintenanceJobsForTodayAsync(today);
        foreach (var jobsPerCustomer in jobsToNotify.GroupBy(job => job.CustomerId))
        {
            // build notification body
            string customerId = jobsPerCustomer.Key;
            Customer customer = await _repo.GetCustomerAsync(customerId);
            StringBuilder body = new StringBuilder();
            body.AppendLine($"Dear {customer.Name},\n");
            body.AppendLine($"We would like to remind you that you have an appointment with us for maintenance on your vehicle(s):\n");
            foreach (MaintenanceJob job in jobsPerCustomer)
            {
                body.AppendLine($"- {job.StartTime.ToString("dd-MM-yyyy")} at {job.StartTime.ToString("HH:mm")} : " +
                    $"{job.Description} on vehicle with license-number {job.LicenseNumber}");
            }

            body.AppendLine($"\nPlease make sure you're present at least 10 minutes before the (first) job is planned.");
            body.AppendLine($"Once arrived, you can notify your arrival at our front-desk.\n");
            body.AppendLine($"Greetings,\n");
            body.AppendLine($"The PitStop crew");

            Log.Information("Sent notification to: {CustomerName}", customer.Name);

            // send notification
            await _emailNotifier.SendEmailAsync(
                customer.EmailAddress, "noreply@pitstop.nl", "Vehicle maintenance reminder", body.ToString());

            // remove jobs for which a notification was sent
            await _repo.RemoveMaintenanceJobsAsync(jobsPerCustomer.Select(job => job.JobId));
        }
    }
}
