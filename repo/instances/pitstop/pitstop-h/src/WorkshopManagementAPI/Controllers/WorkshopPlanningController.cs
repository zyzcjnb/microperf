namespace Pitstop.WorkshopManagementAPI.Controllers;

[Route("/api/[controller]")]
public class WorkshopPlanningController : Controller
{
    private static readonly HttpClient _httpClient = new HttpClient();

    private readonly IEventSourceRepository<WorkshopPlanning> _planningRepo;
    private readonly string _hubApi;

    public WorkshopPlanningController(IEventSourceRepository<WorkshopPlanning> planningRepo, IConfiguration config)
    {
        _planningRepo = planningRepo;
        _hubApi = config.GetValue<string>("HubApi") ?? "http://customermanagementapi:5100";
    }

    [HttpGet]
    [Route("{planningDate}", Name = "GetByDate")]
    public async Task<IActionResult> GetByDate(DateTime planningDate)
    {
        try
        {
            var aggregateId = WorkshopPlanningId.Create(planningDate);
            var planning = await _planningRepo.GetByIdAsync(aggregateId);
            if (planning == null)
            {
                return NotFound();
            }

            return Ok(planning.MapToDTO());
        }
        catch (Exception ex)
        {
            Log.Error(ex.ToString());
            throw;
        }
    }

    [HttpGet]
    [Route("{planningDate}/jobs/{jobId}")]
    public async Task<IActionResult> GetMaintenanceJobAsync(DateTime planningDate, Guid jobId)
    {
        if (ModelState.IsValid)
        {
            try
            {
                // get planning
                var aggregateId = WorkshopPlanningId.Create(planningDate);
                var planning = await _planningRepo.GetByIdAsync(aggregateId);
                if (planning == null || planning.Jobs == null)
                {
                    return NotFound();
                }
                // get job
                var job = planning.Jobs.FirstOrDefault(j => j.Id == jobId);
                if (job == null)
                {
                    return NotFound();
                }
                return Ok(job.MapToDTO());
            }
            catch (Exception ex)
            {
                Log.Error(ex.ToString());
                throw;
            }
        }
        return BadRequest();
    }

    [HttpPost]
    [Route("{planningDate}/jobs")]
    public Task<IActionResult> PlanMaintenanceJobAsync(DateTime planningDate) =>
        ProxySendAsync(HttpMethod.Post, $"{_hubApi}/api/hub/workshopplanning/{planningDate:yyyy-MM-dd}/jobs");

    [HttpPut]
    [Route("{planningDate}/jobs/{jobId}/finish")]
    public Task<IActionResult> FinishMaintenanceJobAsync(DateTime planningDate, Guid jobId) =>
        ProxySendAsync(HttpMethod.Put, $"{_hubApi}/api/hub/workshopplanning/{planningDate:yyyy-MM-dd}/jobs/{jobId}/finish");

    private async Task<IActionResult> ProxySendAsync(HttpMethod method, string url)
    {
        using var request = new HttpRequestMessage(method, url);
        request.Content = new StreamContent(Request.Body);
        if (Request.ContentType != null)
        {
            request.Content.Headers.TryAddWithoutValidation("Content-Type", Request.ContentType);
        }
        var response = await _httpClient.SendAsync(request);
        var content = await response.Content.ReadAsStringAsync();
        return new ContentResult
        {
            Content = content,
            ContentType = "application/json",
            StatusCode = (int)response.StatusCode
        };
    }
}
