namespace Pitstop.Services;

public class AckListenerWorker : Microsoft.Extensions.Hosting.IHostedService, Pitstop.Infrastructure.Messaging.IMessageHandlerCallback
{
    private readonly Pitstop.Infrastructure.Messaging.IMessageHandler _messageHandler;
    private readonly AckAwaiter _ackAwaiter;

    public AckListenerWorker(Pitstop.Infrastructure.Messaging.IMessageHandler messageHandler, AckAwaiter ackAwaiter)
    {
        _messageHandler = messageHandler;
        _ackAwaiter = ackAwaiter;
    }

    public System.Threading.Tasks.Task StartAsync(System.Threading.CancellationToken cancellationToken)
    {
        _messageHandler.Start(this);
        return System.Threading.Tasks.Task.CompletedTask;
    }

    public System.Threading.Tasks.Task StopAsync(System.Threading.CancellationToken cancellationToken)
    {
        _messageHandler.Stop();
        return System.Threading.Tasks.Task.CompletedTask;
    }

    public System.Threading.Tasks.Task<bool> HandleMessageAsync(string messageType, string message)
    {
        try
        {
            if (messageType == "EventAck")
            {
                var messageObject = Newtonsoft.Json.Linq.JObject.Parse(message);
                var ack = messageObject.ToObject<Pitstop.Events.EventAck>();
                if (ack != null)
                {
                    _ackAwaiter.Acknowledge(ack.CorrelationId);
                }
            }
        }
        catch (System.Exception ex)
        {
            Serilog.Log.Error(ex, "Error while handling EventAck message.");
        }
        return System.Threading.Tasks.Task.FromResult(true);
    }
}
