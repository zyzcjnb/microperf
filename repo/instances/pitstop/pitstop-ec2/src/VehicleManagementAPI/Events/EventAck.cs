namespace Pitstop.Events;

public class EventAck : Infrastructure.Messaging.Event
{
    public readonly System.Guid CorrelationId;
    public readonly string EventType;

    public EventAck(System.Guid messageId, System.Guid correlationId, string eventType) : base(messageId)
    {
        CorrelationId = correlationId;
        EventType = eventType;
    }
}
