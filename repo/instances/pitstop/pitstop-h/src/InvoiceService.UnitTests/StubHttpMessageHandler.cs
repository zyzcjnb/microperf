namespace InvoiceService.UnitTests;

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
    public static (IHttpClientFactory Factory, Microsoft.Extensions.Options.IOptions<Pitstop.InvoiceService.ApiEndpoints> Options) Create(
        object hubResponse)
    {
        var json = JsonConvert.SerializeObject(hubResponse);
        var handler = new StubHttpMessageHandler(_ => new HttpResponseMessage(System.Net.HttpStatusCode.OK)
        {
            Content = new StringContent(json, System.Text.Encoding.UTF8, "application/json")
        });

        var factoryMock = new Mock<IHttpClientFactory>();
        factoryMock
            .Setup(f => f.CreateClient(It.IsAny<string>()))
            .Returns(new HttpClient(handler));

        var options = Microsoft.Extensions.Options.Options.Create(
            new Pitstop.InvoiceService.ApiEndpoints { CustomerManagementApi = "http://hub" });

        return (factoryMock.Object, options);
    }
}
