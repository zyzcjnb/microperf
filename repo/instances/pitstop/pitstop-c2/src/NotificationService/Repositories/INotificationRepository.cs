namespace Pitstop.NotificationService.Repositories;

public interface INotificationRepository
{
    Task RegisterCustomerAsync(Customer customer);
    Task RegisterMaintenanceJobAsync(MaintenanceJob job);
    Task<IEnumerable<MaintenanceJob>> GetMaintenanceJobsForTodayAsync(DateTime date);
    Task<Customer> GetCustomerAsync(string customerId);
    Task<IEnumerable<Customer>> GetCustomersByIdsAsync(IEnumerable<string> customerIds);
    Task RemoveMaintenanceJobsAsync(IEnumerable<string> jobIds);
}
