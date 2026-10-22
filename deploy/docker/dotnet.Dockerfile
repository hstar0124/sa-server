# syntax=docker/dockerfile:1.7
# .NET services: one publish stage, three runtime images picked with --target (backend,
# eventworker, migrator). Contracts are compiled from contracts/ at build time; nothing generated
# is checked in. Restore uses the lock files, as CI does.
FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src
COPY server/dotnet/ server/dotnet/
COPY contracts/ contracts/
# Only the three published projects (and what they reference): the solution also lists tests and
# tools that are not in this context.
RUN for project in GameBackend EventWorker Migrator; do dotnet restore "server/dotnet/$project" --locked-mode; done \
    && dotnet publish server/dotnet/GameBackend -c Release --no-restore -o /out/backend \
    && dotnet publish server/dotnet/EventWorker -c Release --no-restore -o /out/eventworker \
    && dotnet publish server/dotnet/Migrator -c Release --no-restore -o /out/migrator

# One runtime base for all three. Npgsql probes the Kerberos library at start-up; without it every
# process logs a load error.
FROM mcr.microsoft.com/dotnet/aspnet:10.0 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libgssapi-krb5-2 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
USER $APP_UID

FROM runtime AS backend
COPY --from=build /out/backend ./
# The Backend checks kills and character classes against the same content the zones ship.
COPY game-data ./game-data
ENV GameData__ContentDir=/app/game-data
ENTRYPOINT ["dotnet", "GameBackend.dll"]

FROM runtime AS eventworker
COPY --from=build /out/eventworker ./
ENTRYPOINT ["dotnet", "EventWorker.dll"]

FROM runtime AS migrator
COPY --from=build /out/migrator ./
COPY database/migrations ./migrations
# The connection string comes from MIGRATOR_connection (a Secret in K3s).
ENTRYPOINT ["dotnet", "Migrator.dll", "--scripts", "/app/migrations"]
