namespace Pitstop.WorkshopManagementEventHandler.Events;

public class MaintenanceJobFinished : Event
{
    public readonly Guid JobId;
    public readonly DateTime PlanningDate;

    public MaintenanceJobFinished(Guid messageId, Guid jobId, DateTime planningDate) : base(messageId)
    {
        JobId = jobId;
        PlanningDate = planningDate;
    }
}
