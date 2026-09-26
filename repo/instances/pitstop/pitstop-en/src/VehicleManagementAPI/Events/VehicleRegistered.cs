namespace Pitstop.VehicleManagement.Events;

public class VehicleRegistered : Event
{
    public readonly string LicenseNumber;

    public VehicleRegistered(Guid messageId, string licenseNumber) :
        base(messageId)
    {
        LicenseNumber = licenseNumber;
    }

    public static VehicleRegistered FromCommand(RegisterVehicle command)
    {
        return new VehicleRegistered(
            Guid.NewGuid(),
            command.LicenseNumber
        );
    }
}
