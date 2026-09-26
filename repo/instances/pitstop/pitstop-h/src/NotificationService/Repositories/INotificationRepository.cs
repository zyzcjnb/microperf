namespace Pitstop.NotificationService.Repositories;

public interface INotificationRepository
{
    Task RegisterMaintenanceJobAsync(MaintenanceJob job);
    Task<IEnumerable<MaintenanceJob>> GetMaintenanceJobsForTodayAsync(DateTime date);
    Task RemoveMaintenanceJobsAsync(IEnumerable<string> jobIds);
}