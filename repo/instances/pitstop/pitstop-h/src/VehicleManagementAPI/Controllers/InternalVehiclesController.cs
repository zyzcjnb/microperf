namespace Pitstop.VehicleManagement.Controllers;

[Route("/api/internal/vehicles")]
public class InternalVehiclesController : Controller
{
    private const string NUMBER_PATTERN = @"^((\d{1,3}|[a-z]{1,3})-){2}(\d{1,3}|[a-z]{1,3})$";
    IMessagePublisher _messagePublisher;
    VehicleManagementDBContext _dbContext;

    public InternalVehiclesController(VehicleManagementDBContext dbContext, IMessagePublisher messagePublisher)
    {
        _dbContext = dbContext;
        _messagePublisher = messagePublisher;
    }

    [HttpPost]
    public async Task<IActionResult> RegisterAsync([FromBody] RegisterVehicle command)
    {
        try
        {
            if (ModelState.IsValid)
            {
                // check invariants
                if (!Regex.IsMatch(command.LicenseNumber, NUMBER_PATTERN, RegexOptions.IgnoreCase))
                {
                    return BadRequest($"The specified license-number '{command.LicenseNumber}' was not in the correct format.");
                }

                // insert vehicle
                Vehicle vehicle = command.MapToVehicle();
                _dbContext.Vehicles.Add(vehicle);
                await _dbContext.SaveChangesAsync();

                // send event
                var e = VehicleRegistered.FromCommand(command);
                await _messagePublisher.PublishMessageAsync(e.MessageType, e, "");

                //return result
                return CreatedAtRoute("GetByLicenseNumber", new { licenseNumber = vehicle.LicenseNumber }, vehicle);
            }
            return BadRequest();
        }
        catch (DbUpdateException)
        {
            ModelState.AddModelError("", "Unable to save changes. " +
                "Try again, and if the problem persists " +
                "see your system administrator.");
            return StatusCode(StatusCodes.Status500InternalServerError);
        }
    }
}
