namespace Pitstop.TimeService;

public class TimeWorker : IHostedService
{
    DateTime _lastCheck;
    CancellationTokenSource _cancellationTokenSource;
    Task _task;
    IMessagePublisher _messagePublisher;

    public TimeWorker(IMessagePublisher messagePublisher)
    {
        _cancellationTokenSource = new CancellationTokenSource();
        _lastCheck = DateTime.Now;
        _messagePublisher = messagePublisher;
    }

    public Task StartAsync(CancellationToken cancellationToken)
    {
        _task = Worker(_cancellationTokenSource.Token);
        return Task.CompletedTask;
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        _cancellationTokenSource.Cancel();
        if (_task != null)
        {
            try
            {
                await _task;
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    private async Task Worker(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            if (DateTime.Now.Subtract(_lastCheck).Days > 0)
            {
                Log.Information($"Day has passed!");
                _lastCheck = DateTime.Now;
                DateTime passedDay = _lastCheck.AddDays(-1);
                DayHasPassed e = new DayHasPassed(Guid.NewGuid());
                await _messagePublisher.PublishMessageAsync(e.MessageType, e, "");
            }
            try
            {
                await Task.Delay(10000, cancellationToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }
}
