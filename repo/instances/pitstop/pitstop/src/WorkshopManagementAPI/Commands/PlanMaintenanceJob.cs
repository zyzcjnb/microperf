namespace Pitstop.WorkshopManagementAPI.Commands;

public class CustomerInfo
{
    public string Id { get; set; }
    public string Name { get; set; }
    public string TelephoneNumber { get; set; }
}

public class VehicleInfo
{
    public string LicenseNumber { get; set; }
    public string Brand { get; set; }
    public string Type { get; set; }
}

public class PlanMaintenanceJob : Command
{
    public readonly Guid JobId;
    public readonly DateTime StartTime;
    public readonly DateTime EndTime;
    public readonly CustomerInfo CustomerInfo;
    public readonly VehicleInfo VehicleInfo;
    public readonly string Description;

    public PlanMaintenanceJob(Guid messageId, Guid jobId, DateTime startTime, DateTime endTime,
        CustomerInfo customerInfo, VehicleInfo vehicleInfo, string description) : base(messageId)
    {
        JobId = jobId;
        StartTime = startTime;
        EndTime = endTime;
        CustomerInfo = customerInfo;
        VehicleInfo = vehicleInfo;
        Description = description;
    }
}
