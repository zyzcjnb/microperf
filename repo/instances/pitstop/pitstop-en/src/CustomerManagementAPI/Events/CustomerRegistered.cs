namespace Pitstop.CustomerManagementAPI.Events;

public class CustomerRegistered : Event
{
    public readonly string CustomerId;

    public CustomerRegistered(Guid messageId, string customerId) : base(messageId)
    {
        CustomerId = customerId;
    }
}
