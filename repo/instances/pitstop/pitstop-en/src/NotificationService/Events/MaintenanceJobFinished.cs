namespace Pitstop.NotificationService.Events;

public class MaintenanceJobFinished : Event
{
    public readonly string JobId;
    public readonly DateTime PlanningDate;

    public MaintenanceJobFinished(Guid messageId, string jobId, DateTime planningDate) :
        base(messageId)
    {
        JobId = jobId;
        PlanningDate = planningDate;
    }
}
