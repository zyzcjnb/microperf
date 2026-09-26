namespace Pitstop.InvoiceService.Repositories;

public interface IInvoiceRepository
{
    Task RegisterCustomerAsync(Customer customer);
    Task<Customer> GetCustomerAsync(string customerId);
    Task<IEnumerable<Customer>> GetCustomersByIdsAsync(IEnumerable<string> customerIds);
    Task RegisterMaintenanceJobAsync(MaintenanceJob job);
    Task MarkMaintenanceJobAsFinished(string jobId, DateTime startTime, DateTime endTime);
    Task<IEnumerable<MaintenanceJob>> GetMaintenanceJobsToBeInvoicedAsync();
    Task RegisterInvoiceAsync(Invoice invoice);
}
