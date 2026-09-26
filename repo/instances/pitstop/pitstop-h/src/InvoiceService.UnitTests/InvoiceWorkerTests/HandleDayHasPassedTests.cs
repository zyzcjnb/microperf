namespace InvoiceService.UnitTests.InvoiceWorkerTests;

[TestClass]
public class HandleDayHasPassedTests
{
    [TestMethod]
    public async Task HandleMessage_With_DayHasPassed_And_No_Uninvoiced_Jobs_Should_Not_Send_Any_Invoice()
    {
        // arrange
        string json = JsonConvert.SerializeObject(new DayHasPassedEventBuilder().Build());

        Mock<IMessageHandler> messageHandlerMock = new Mock<IMessageHandler>();
        Mock<IInvoiceRepository> repoMock = new Mock<IInvoiceRepository>();
        Mock<IEmailCommunicator> emailMock = new Mock<IEmailCommunicator>();
        var (httpClientFactory, options) = HubStubs.Create(new { });

        repoMock
            .Setup(r => r.GetMaintenanceJobsToBeInvoicedAsync())
            .ReturnsAsync(Enumerable.Empty<MaintenanceJob>());

        InvoiceWorker sut = new InvoiceWorker(messageHandlerMock.Object, repoMock.Object, emailMock.Object,
            httpClientFactory, options);

        // act
        await sut.HandleMessageAsync("DayHasPassed", json);

        // assert
        repoMock.VerifyAll();
        repoMock.VerifyNoOtherCalls();
        emailMock.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task HandleMessage_With_DayHasPassed_And_One_Uninvoiced_Job_Should_Send_Invoice_And_Register_It()
    {
        // arrange
        string json = JsonConvert.SerializeObject(new DayHasPassedEventBuilder().Build());

        CustomerBuilder customerBuilder = new CustomerBuilder();
        Customer customer = customerBuilder.Build();

        MaintenanceJob job = new MaintenanceJobBuilder()
            .WithCustomerId(customer.CustomerId)
            .WithStartTime(DateTime.Today.AddHours(8))
            .WithEndTime(DateTime.Today.AddHours(10))
            .Build();

        Mock<IMessageHandler> messageHandlerMock = new Mock<IMessageHandler>();
        Mock<IInvoiceRepository> repoMock = new Mock<IInvoiceRepository>();
        Mock<IEmailCommunicator> emailMock = new Mock<IEmailCommunicator>();
        var (httpClientFactory, options) = HubStubs.Create(customer);

        repoMock
            .Setup(r => r.GetMaintenanceJobsToBeInvoicedAsync())
            .ReturnsAsync(new[] { job });

        repoMock
            .Setup(r => r.RegisterInvoiceAsync(It.Is<Invoice>(i =>
                i.CustomerId == customer.CustomerId &&
                i.JobIds == job.JobId &&
                // 2 hours at €18.50/hour = €37.00
                i.Amount == 37.00M)))
            .Returns(Task.CompletedTask);

        emailMock
            .Setup(e => e.SendEmailAsync(It.IsAny<MailMessage>()))
            .Returns(Task.CompletedTask);

        InvoiceWorker sut = new InvoiceWorker(messageHandlerMock.Object, repoMock.Object, emailMock.Object,
            httpClientFactory, options);

        // act
        await sut.HandleMessageAsync("DayHasPassed", json);

        // assert
        repoMock.VerifyAll();
        repoMock.VerifyNoOtherCalls();
        emailMock.VerifyAll();
        emailMock.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task HandleMessage_With_DayHasPassed_And_Multiple_Jobs_For_Same_Customer_Should_Send_One_Invoice_Covering_All_Jobs()
    {
        // arrange
        string json = JsonConvert.SerializeObject(new DayHasPassedEventBuilder().Build());

        CustomerBuilder customerBuilder = new CustomerBuilder();
        Customer customer = customerBuilder.Build();

        MaintenanceJob job1 = new MaintenanceJobBuilder()
            .WithCustomerId(customer.CustomerId)
            .WithStartTime(DateTime.Today.AddHours(8))
            .WithEndTime(DateTime.Today.AddHours(10))
            .Build();

        MaintenanceJob job2 = new MaintenanceJobBuilder()
            .WithCustomerId(customer.CustomerId)
            .WithStartTime(DateTime.Today.AddHours(13))
            .WithEndTime(DateTime.Today.AddHours(14))
            .Build();

        Mock<IMessageHandler> messageHandlerMock = new Mock<IMessageHandler>();
        Mock<IInvoiceRepository> repoMock = new Mock<IInvoiceRepository>();
        Mock<IEmailCommunicator> emailMock = new Mock<IEmailCommunicator>();
        var (httpClientFactory, options) = HubStubs.Create(customer);

        repoMock
            .Setup(r => r.GetMaintenanceJobsToBeInvoicedAsync())
            .ReturnsAsync(new[] { job1, job2 });

        repoMock
            .Setup(r => r.RegisterInvoiceAsync(It.Is<Invoice>(i =>
                i.CustomerId == customer.CustomerId &&
                // Both job IDs joined with a pipe
                i.JobIds == $"{job1.JobId}|{job2.JobId}" &&
                // job1: 2h × €18.50 = €37.00, job2: 1h × €18.50 = €18.50 → total: €55.50
                i.Amount == 55.50M)))
            .Returns(Task.CompletedTask);

        emailMock
            .Setup(e => e.SendEmailAsync(It.IsAny<MailMessage>()))
            .Returns(Task.CompletedTask);

        InvoiceWorker sut = new InvoiceWorker(messageHandlerMock.Object, repoMock.Object, emailMock.Object,
            httpClientFactory, options);

        // act
        await sut.HandleMessageAsync("DayHasPassed", json);

        // assert
        repoMock.VerifyAll();
        repoMock.VerifyNoOtherCalls();
        // One email and one invoice registration: one call each
        emailMock.VerifyAll();
        emailMock.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task HandleMessage_With_DayHasPassed_And_Jobs_For_Different_Customers_Should_Send_Separate_Invoices_Per_Customer()
    {
        // arrange
        string json = JsonConvert.SerializeObject(new DayHasPassedEventBuilder().Build());

        Customer customer1 = new CustomerBuilder().Build();
        Customer customer2 = new CustomerBuilder().Build();

        MaintenanceJob job1 = new MaintenanceJobBuilder()
            .WithCustomerId(customer1.CustomerId)
            .WithStartTime(DateTime.Today.AddHours(8))
            .WithEndTime(DateTime.Today.AddHours(10))
            .Build();

        MaintenanceJob job2 = new MaintenanceJobBuilder()
            .WithCustomerId(customer2.CustomerId)
            .WithStartTime(DateTime.Today.AddHours(9))
            .WithEndTime(DateTime.Today.AddHours(11))
            .Build();

        Mock<IMessageHandler> messageHandlerMock = new Mock<IMessageHandler>();
        Mock<IInvoiceRepository> repoMock = new Mock<IInvoiceRepository>();
        Mock<IEmailCommunicator> emailMock = new Mock<IEmailCommunicator>();

        // hub returns whichever customer is requested
        var handler = new StubHttpMessageHandler(req =>
        {
            var customer = req.RequestUri!.ToString().EndsWith(customer1.CustomerId) ? customer1 : customer2;
            return new HttpResponseMessage(System.Net.HttpStatusCode.OK)
            {
                Content = new StringContent(JsonConvert.SerializeObject(customer),
                    System.Text.Encoding.UTF8, "application/json")
            };
        });
        var factoryMock = new Mock<IHttpClientFactory>();
        factoryMock.Setup(f => f.CreateClient(It.IsAny<string>())).Returns(new HttpClient(handler));
        var options = Microsoft.Extensions.Options.Options.Create(
            new Pitstop.InvoiceService.ApiEndpoints { CustomerManagementApi = "http://hub" });

        repoMock
            .Setup(r => r.GetMaintenanceJobsToBeInvoicedAsync())
            .ReturnsAsync(new[] { job1, job2 });

        repoMock
            .Setup(r => r.RegisterInvoiceAsync(It.Is<Invoice>(i => i.CustomerId == customer1.CustomerId)))
            .Returns(Task.CompletedTask);

        repoMock
            .Setup(r => r.RegisterInvoiceAsync(It.Is<Invoice>(i => i.CustomerId == customer2.CustomerId)))
            .Returns(Task.CompletedTask);

        emailMock
            .Setup(e => e.SendEmailAsync(It.IsAny<MailMessage>()))
            .Returns(Task.CompletedTask);

        InvoiceWorker sut = new InvoiceWorker(messageHandlerMock.Object, repoMock.Object, emailMock.Object,
            factoryMock.Object, options);

        // act
        await sut.HandleMessageAsync("DayHasPassed", json);

        // assert
        repoMock.VerifyAll();
        repoMock.VerifyNoOtherCalls();
        // Two separate emails: one per customer
        emailMock.Verify(e => e.SendEmailAsync(It.IsAny<MailMessage>()), Times.Exactly(2));
        emailMock.VerifyNoOtherCalls();
    }
}
