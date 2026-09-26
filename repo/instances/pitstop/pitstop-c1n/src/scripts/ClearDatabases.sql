IF DB_ID('CustomerIdentity') IS NOT NULL delete from CustomerIdentity.dbo.CustomerIdentity;
IF DB_ID('CustomerAddress') IS NOT NULL delete from CustomerAddress.dbo.CustomerAddress;
IF DB_ID('CustomerTelephone') IS NOT NULL delete from CustomerTelephone.dbo.CustomerTelephone;
IF DB_ID('CustomerEmail') IS NOT NULL delete from CustomerEmail.dbo.CustomerEmail;

IF DB_ID('Invoicing') IS NOT NULL delete from Invoicing.dbo.Invoice;
IF DB_ID('Invoicing') IS NOT NULL delete from Invoicing.dbo.Customer;
IF DB_ID('Invoicing') IS NOT NULL delete from Invoicing.dbo.MaintenanceJob;

IF DB_ID('Notification') IS NOT NULL delete from Notification.dbo.Customer;
IF DB_ID('Notification') IS NOT NULL delete from Notification.dbo.MaintenanceJob;

IF DB_ID('VehicleInfo') IS NOT NULL delete from VehicleInfo.dbo.VehicleInfo;
IF DB_ID('VehicleOwner') IS NOT NULL delete from VehicleOwner.dbo.VehicleOwner;

IF DB_ID('WorkshopManagement') IS NOT NULL delete from WorkshopManagement.dbo.MaintenanceJob;
IF DB_ID('WorkshopManagement') IS NOT NULL delete from WorkshopManagement.dbo.Vehicle;
IF DB_ID('WorkshopManagement') IS NOT NULL delete from WorkshopManagement.dbo.Customer;

IF DB_ID('WorkshopManagementEventStore') IS NOT NULL delete from WorkshopManagementEventStore.dbo.WorkshopPlanningEvent;
IF DB_ID('WorkshopManagementEventStore') IS NOT NULL delete from WorkshopManagementEventStore.dbo.WorkshopPlanning;
