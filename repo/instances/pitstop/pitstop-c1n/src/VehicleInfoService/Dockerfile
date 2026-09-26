FROM pitstop-dotnet-sdk-base:1.0 AS build-env
WORKDIR /app

COPY *.csproj ./
RUN dotnet restore

COPY . ./
RUN dotnet publish -c Release -o out

FROM pitstop-dotnet-aspnet-base:1.0
WORKDIR /app
COPY --from=build-env /app/out .

EXPOSE 5050/tcp
ENV ASPNETCORE_URLS=http://*:5050
HEALTHCHECK --interval=30s --timeout=3s --retries=1 CMD curl --silent --fail http://localhost:5050/hc || exit 1

ENTRYPOINT ["dotnet", "Pitstop.VehicleInfoService.dll"]
