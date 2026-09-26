namespace Pitstop.WorkshopManagementAPI.CommandHandlers;

public class FinishMaintenanceJobCommandHandler : IFinishMaintenanceJobCommandHandler
{
    IMessagePublisher _messagePublisher;
    IEventSourceRepository<WorkshopPlanning> _planningRepo;

    public FinishMaintenanceJobCommandHandler(IMessagePublisher messagePublisher, IEventSourceRepository<WorkshopPlanning> planningRepo)
    {
        _messagePublisher = messagePublisher;
        _planningRepo = planningRepo;
    }

    public async Task<WorkshopPlanning> HandleCommandAsync(DateTime planningDate, FinishMaintenanceJob command)
    {
        // get planning
        var aggregateId = WorkshopPlanningId.Create(planningDate);
        var planning = await _planningRepo.GetByIdAsync(aggregateId);
        if (planning == null)
        {
            return null;
        }

        // handle command
        planning.FinishMaintenanceJob(command);

        // persist
        IEnumerable<Event> events = planning.GetEvents();
        await _planningRepo.SaveAsync(
            planning.Id, planning.OriginalVersion, planning.Version, events);

        var thinEvent = new IntegrationEvents.MaintenanceJobFinished(
            command.MessageId, command.JobId, (DateTime)planning.Id);
        await _messagePublisher.PublishMessageAsync(thinEvent.MessageType, thinEvent, "");

        // return result
        return planning;
    }
}
