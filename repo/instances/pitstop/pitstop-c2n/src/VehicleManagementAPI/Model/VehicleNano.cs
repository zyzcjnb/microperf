namespace Pitstop.VehicleManagement.Model;

public class VehicleInfo
{
    public string LicenseNumber { get; set; }
    public string Brand { get; set; }
    public string Type { get; set; }
}

public class VehicleOwner
{
    public string LicenseNumber { get; set; }
    public string OwnerId { get; set; }
}
