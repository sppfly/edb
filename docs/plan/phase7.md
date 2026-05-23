# Phase 7 — Network 🔲

PostgreSQL wire protocol v3, simple query mode only.

## Message Flow

```
Client                         Server
  │── StartupMessage ─────────►│
  │◄── AuthenticationOk ───────│
  │◄── ReadyForQuery ──────────│
  │── Query("SELECT ...") ─────►│
  │◄── RowDescription ─────────│
  │◄── DataRow (×N) ───────────│
  │◄── CommandComplete ─────────│
  │◄── ReadyForQuery ──────────│
```

## Deliverables

- [ ] TCP listener + connection handler (one thread per connection initially)
- [ ] Startup / auth (trust mode for now)
- [ ] `Query` → parse → execute → `RowDescription` + `DataRow` + `CommandComplete`
- [ ] `ErrorResponse` for query errors
- [ ] Integration test with `psql` client
