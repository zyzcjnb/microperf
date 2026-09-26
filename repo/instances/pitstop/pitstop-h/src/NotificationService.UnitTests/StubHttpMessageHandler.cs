namespace NotificationService.UnitTests;

public class StubHttpMessageHandler : HttpMessageHandler
{
    private readonly Func<HttpRequestMessage, HttpResponseMessage> _responder;

    public StubHttpMessageHandler(Func<HttpRequestMessage, HttpResponseMessage> responder)
    {
        _responder = responder;
    }

    protected override Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request, CancellationToken cancellationToken)
        => Task.FromResult(_responder(request));
}

public static class HubStubs
{
    public static (IHttpClientFactory Factory, Microsoft.Extensions.Options.IOptions<Pitstop.NotificationService.ApiEndpoints> Options) Create(
        params object[] customers)
    {
        var handler = new StubHttpMessageHandler(req =>
        {
            var uri = req.RequestUri!.ToString();
            var customer = customers.FirstOrDefault(c =>
                uri.EndsWith((string)c.GetType().GetProperty("CustomerId")!.GetValue(c)!));
            return new HttpResponseMessage(System.Net.HttpStatusCode.OK)
            {
                Content = new StringContent(JsonConvert.SerializeObject(customer ?? customers.First()),
                    System.Text.Encoding.UTF8, "application/json")
            };
        });

        var factoryMock = new Mock<IHttpClientFactory>();
        factoryMock
            .Setup(f => f.CreateClient(It.IsAny<string>()))
            .Returns(new HttpClient(handler));

        var options = Microsoft.Extensions.Options.Options.Create(
            new Pitstop.NotificationService.ApiEndpoints { CustomerManagementApi = "http://hub" });

        return (factoryMock.Object, options);
    }
}
