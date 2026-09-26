namespace Pitstop.WorkshopManagementAPI.Controllers;

[Route("/api/internal/workshopplanning")]
public class InternalWorkshopPlanningController : Controller
{
    private readonly IPlanMaintenanceJobCommandHandler _planMaintenanceJobCommandHandler;
    private readonly IFinishMaintenanceJobCommandHandler _finishMaintenanceJobCommandHandler;

    public InternalWorkshopPlanningController(
        IPlanMaintenanceJobCommandHandler planMaintenanceJobCommandHandler,
        IFinishMaintenanceJobCommandHandler finishMaintenanceJobCommand)
    {
        _planMaintenanceJobCommandHandler = planMaintenanceJobCommandHandler;
        _finishMaintenanceJobCommandHandler = finishMaintenanceJobCommand;
    }

    [HttpPost]
    [Route("{planningDate}/jobs")]
    public async Task<IActionResult> PlanMaintenanceJobAsync(DateTime planningDate, [FromBody] PlanMaintenanceJob command)
    {
        try
        {
            if (ModelState.IsValid)
            {
                try
                {
                    // handle command
                    WorkshopPlanning planning = await
                        _planMaintenanceJobCommandHandler.HandleCommandAsync(planningDate, command);

                    // handle result    
                    if (planning == null)
                    {
                        return NotFound();
                    }

                    // return result
                    return CreatedAtRoute("GetByDate", new { planningDate = planning.Id }, planning.MapToDTO());
                }
                catch (BusinessRuleViolationException ex)
                {
                    return StatusCode(StatusCodes.Status409Conflict, new BusinessRuleViolation { ErrorMessage = ex.Message });
                }
            }
            return BadRequest();
        }
        catch (ConcurrencyException)
        {
            string errorMessage = "Unable to save changes. " +
                "Try again, and if the problem persists " +
                "see your system administrator.";
            Log.Error(errorMessage);
            ModelState.AddModelError("ErrorMessage", errorMessage);
            return StatusCode(StatusCodes.Status500InternalServerError);
        }
    }

    [HttpPut]
    [Route("{planningDate}/jobs/{jobId}/finish")]
    public async Task<IActionResult> FinishMaintenanceJobAsync(DateTime planningDate, Guid jobId, [FromBody] FinishMaintenanceJob command)
    {
        try
        {
            if (ModelState.IsValid)
            {
                // handle command
                WorkshopPlanning planning = await
                    _finishMaintenanceJobCommandHandler.HandleCommandAsync(planningDate, command);

                // handle result    
                if (planning == null)
                {
                    return NotFound();
                }

                // return result
                return Ok();
            }
            return BadRequest();
        }
        catch (ConcurrencyException)
        {
            string errorMessage = "Unable to save changes. " +
                "Try again, and if the problem persists " +
                "see your system administrator.";
            Log.Error(errorMessage);
            ModelState.AddModelError("", errorMessage);
            return StatusCode(StatusCodes.Status500InternalServerError);
        }
    }
}
