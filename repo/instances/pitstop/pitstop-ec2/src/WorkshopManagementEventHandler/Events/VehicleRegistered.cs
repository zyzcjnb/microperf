namespace Pitstop.WorkshopManagementEventHandler.Events;

public class VehicleRegistered : Event
{
    public readonly string LicenseNumber;

    public VehicleRegistered(Guid messageId, string licenseNumber) :
        base(messageId)
    {
        LicenseNumber = licenseNumber;
    }
}
