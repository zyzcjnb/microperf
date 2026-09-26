namespace Pitstop.Services;

public class AckAwaiter
{
    private readonly System.Collections.Concurrent.ConcurrentDictionary<System.Guid, PendingAck> _pending = new();

    private class PendingAck
    {
        public System.Threading.Tasks.TaskCompletionSource<bool> Tcs { get; } = new();
        public int Remaining;
    }

    public async System.Threading.Tasks.Task WaitForAcksAsync(System.Guid correlationId, int count, System.TimeSpan timeout)
    {
        if (count <= 0)
        {
            return;
        }

        var pending = new PendingAck { Remaining = count };
        _pending[correlationId] = pending;

        try
        {
            var completed = await System.Threading.Tasks.Task.WhenAny(
                pending.Tcs.Task,
                System.Threading.Tasks.Task.Delay(timeout));
        }
        finally
        {
            _pending.TryRemove(correlationId, out _);
        }
    }

    public void Acknowledge(System.Guid correlationId)
    {
        if (_pending.TryGetValue(correlationId, out var pending))
        {
            var remaining = System.Threading.Interlocked.Decrement(ref pending.Remaining);
            if (remaining <= 0)
            {
                pending.Tcs.TrySetResult(true);
            }
        }
    }
}
