---
description: 'Best practices for designing robust, scalable, and maintainable APIs across different protocols and architectures.'
applyTo: '**'
---

# API Design Best Practices

## Overview
This document provides comprehensive guidelines for designing high-quality APIs that are consistent, scalable, maintainable, and developer-friendly.

## REST API Design Principles

### Resource-Based Design
- Use nouns for resource names (not verbs)
- Use plural nouns for collections
- Use HTTP methods appropriately:
  - `GET` - Retrieve resources
  - `POST` - Create resources
  - `PUT` - Update entire resources
  - `PATCH` - Partial updates
  - `DELETE` - Remove resources

### URL Structure Examples
```
✅ Good:
GET /api/v1/users
GET /api/v1/users/123
POST /api/v1/users
PUT /api/v1/users/123
DELETE /api/v1/users/123

❌ Bad:
GET /api/v1/getUsers
POST /api/v1/createUser
GET /api/v1/userDetails?id=123
```

### HTTP Status Codes
- `200 OK` - Success
- `201 Created` - Resource created
- `204 No Content` - Success with no response body
- `400 Bad Request` - Invalid request
- `401 Unauthorized` - Authentication required
- `403 Forbidden` - Authorization failed
- `404 Not Found` - Resource not found
- `409 Conflict` - Resource conflict
- `422 Unprocessable Entity` - Validation errors
- `500 Internal Server Error` - Server error

## API Versioning

### Versioning Strategies
- **URL Path Versioning**: `/api/v1/users`
- **Header Versioning**: `Accept: application/vnd.api.v1+json`
- **Query Parameter**: `/api/users?version=1`

### Best Practice
Use URL path versioning for clarity and simplicity.

## Request/Response Design

### Request Structure
```json
{
  "data": {
    "type": "user",
    "attributes": {
      "name": "John Doe",
      "email": "john@example.com"
    }
  }
}
```

### Response Structure
```json
{
  "data": {
    "id": "123",
    "type": "user",
    "attributes": {
      "name": "John Doe",
      "email": "john@example.com",
      "createdAt": "2023-01-01T00:00:00Z"
    }
  },
  "meta": {
    "timestamp": "2023-01-01T00:00:00Z"
  }
}
```

### Error Response
```json
{
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Invalid email format",
    "details": [
      {
        "field": "email",
        "message": "Must be a valid email address"
      }
    ]
  }
}
```

## Pagination

### Implementation
- Use `limit` and `offset` or `cursor` for large datasets
- Include pagination metadata in response

### Example
```
GET /api/v1/users?limit=10&offset=20
```

```json
{
  "data": [...],
  "meta": {
    "pagination": {
      "total": 100,
      "limit": 10,
      "offset": 20,
      "hasNext": true,
      "hasPrev": true
    }
  }
}
```

## Filtering and Sorting

### Query Parameters
- `filter[field]=value` for filtering
- `sort=field1,-field2` for sorting (minus for descending)

### Examples
```
GET /api/v1/users?filter[name]=john&sort=name,-createdAt
GET /api/v1/posts?filter[status]=published&filter[author]=123
```

## Authentication and Authorization

### Authentication Methods
- **Bearer Tokens**: `Authorization: Bearer <token>`
- **API Keys**: `X-API-Key: <key>`
- **Basic Auth**: `Authorization: Basic <base64>`

### Authorization
- Use role-based access control (RBAC)
- Implement fine-grained permissions
- Validate permissions on each request

## Rate Limiting

### Implementation
- Use headers to communicate limits
- Return `429 Too Many Requests` when exceeded

### Headers
```
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 95
X-RateLimit-Reset: 1638360000
```

## Caching

### HTTP Caching
- Use `ETag` and `Last-Modified` headers
- Implement conditional requests
- Set appropriate `Cache-Control` headers

### API Response Caching
- Cache expensive operations
- Use cache invalidation strategies
- Consider cache-aside pattern

## Documentation

### OpenAPI Specification
Use OpenAPI 3.0+ for API documentation.

### Essential Documentation
- Endpoint descriptions
- Request/response schemas
- Authentication requirements
- Error codes and messages
- Code examples

### Tools
- Swagger/OpenAPI for documentation
- Postman/Insomnia for testing
- API Blueprint for design

## GraphQL API Design

### Schema Design
- Design schema first
- Use appropriate types
- Implement resolvers efficiently

### Best Practices
- Avoid deeply nested queries
- Use pagination for connections
- Implement proper error handling
- Use fragments for reusable queries

### Example Schema
```graphql
type Query {
  user(id: ID!): User
  users(limit: Int, offset: Int): [User!]!
}

type User {
  id: ID!
  name: String!
  email: String!
  posts: [Post!]!
}

type Mutation {
  createUser(input: CreateUserInput!): User!
}
```

## gRPC API Design

### Protocol Buffers
- Define messages clearly
- Use appropriate field types
- Follow naming conventions

### Best Practices
- Use streaming for large data
- Implement proper error codes
- Version services appropriately
- Document services thoroughly

## API Security

### Security Headers
```
Content-Security-Policy: default-src 'self'
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Strict-Transport-Security: max-age=31536000
```

### Input Validation
- Validate all inputs
- Sanitize data
- Use parameterized queries
- Implement rate limiting

### Common Vulnerabilities to Avoid
- SQL injection
- XSS attacks
- CSRF attacks
- Broken authentication
- Sensitive data exposure

## Performance Optimization

### Response Time
- Aim for <200ms for API responses
- Use compression (gzip)
- Implement caching
- Optimize database queries

### Scalability
- Design for horizontal scaling
- Use load balancers
- Implement circuit breakers
- Monitor performance metrics

## Monitoring and Analytics

### Key Metrics
- Response time percentiles
- Error rates
- Throughput
- Cache hit rates

### Tools
- Application Performance Monitoring (APM)
- API gateways with analytics
- Custom dashboards

## API Evolution

### Backward Compatibility
- Never remove fields without versioning
- Add optional fields carefully
- Deprecate gradually

### Deprecation Strategy
1. Mark as deprecated in documentation
2. Add deprecation warnings
3. Remove in future major version

## Testing APIs

### Testing Types
- Unit tests for business logic
- Integration tests for endpoints
- Contract tests for API compatibility
- Load tests for performance

### Tools
- Postman/Newman for automated testing
- Jest/Supertest for Node.js
- pytest-requests for Python
- RestAssured for Java

## Conclusion

Good API design requires careful consideration of usability, performance, security, and maintainability. Follow these guidelines to create APIs that developers love to use and are easy to maintain and scale.