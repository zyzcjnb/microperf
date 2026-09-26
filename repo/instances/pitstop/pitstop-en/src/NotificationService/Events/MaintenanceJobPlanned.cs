namespace Pitstop.NotificationService.Events;

public class MaintenanceJobPlanned : Event
{
    public readonly Guid JobId;
    public readonly DateTime PlanningDate;

    public MaintenanceJobPlanned(Guid messageId, Guid jobId, DateTime planningDate) : base(messageId)
    {
        JobId = jobId;
        PlanningDate = planningDate;
    }
}
