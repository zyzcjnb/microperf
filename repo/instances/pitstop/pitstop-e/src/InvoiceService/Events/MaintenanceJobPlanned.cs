namespace Pitstop.InvoiceService.Events;

public class MaintenanceJobPlanned : Event
{
    public readonly string JobId;
    public readonly DateTime PlanningDate;

    public MaintenanceJobPlanned(Guid messageId, string jobId, DateTime planningDate) : base(messageId)
    {
        JobId = jobId;
        PlanningDate = planningDate;
    }
}
