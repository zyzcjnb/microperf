namespace Pitstop.InvoiceService.Repositories;

public interface IInvoiceRepository
{
    Task RegisterMaintenanceJobAsync(MaintenanceJob job);
    Task MarkMaintenanceJobAsFinished(string jobId, DateTime startTime, DateTime endTime);
    Task<IEnumerable<MaintenanceJob>> GetMaintenanceJobsToBeInvoicedAsync();
    Task RegisterInvoiceAsync(Invoice invoice);
}