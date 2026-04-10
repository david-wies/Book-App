---
description: 'Comprehensive testing strategies and best practices for ensuring code quality, reliability, and maintainability across different testing levels and frameworks.'
applyTo: '**'
---

# Testing Strategies Best Practices

## Overview
This document outlines comprehensive testing strategies to ensure high-quality, reliable, and maintainable code. It covers unit testing, integration testing, end-to-end testing, and testing best practices.

## Testing Pyramid
Follow the testing pyramid approach:
- **Unit Tests** (bottom, most numerous): Test individual functions/methods
- **Integration Tests** (middle): Test component interactions
- **End-to-End Tests** (top, fewest): Test complete user workflows

## Unit Testing Guidelines

### Best Practices
- Test one thing at a time
- Use descriptive test names (Given-When-Then format)
- Test both positive and negative scenarios
- Mock external dependencies
- Keep tests fast and isolated
- Use code coverage tools (aim for 80%+ coverage)

### Example Structure
```javascript
describe('UserService', () => {
  describe('createUser', () => {
    it('should create a new user when valid data is provided', async () => {
      // Arrange
      const validUserData = { name: 'John', email: 'john@example.com' };
      
      // Act
      const result = await userService.createUser(validUserData);
      
      // Assert
      expect(result.success).toBe(true);
      expect(result.user.name).toBe('John');
    });
    
    it('should throw error when email is invalid', async () => {
      // Arrange
      const invalidUserData = { name: 'John', email: 'invalid-email' };
      
      // Act & Assert
      await expect(userService.createUser(invalidUserData))
        .rejects.toThrow('Invalid email format');
    });
  });
});
```

## Integration Testing

### When to Use
- Test database operations
- Test API endpoints
- Test component interactions
- Test external service integrations

### Best Practices
- Use test databases (in-memory or containers)
- Clean up test data between tests
- Test realistic scenarios
- Mock external APIs when possible

## End-to-End Testing

### When to Use
- Test complete user journeys
- Test critical business flows
- Validate UI interactions

### Best Practices
- Use page object pattern
- Keep tests stable and maintainable
- Run in headless mode for CI
- Focus on happy path and critical error scenarios

## Testing Frameworks by Language

### JavaScript/TypeScript
- **Unit**: Jest, Vitest, Mocha + Chai
- **Integration**: Supertest, TestContainers
- **E2E**: Playwright, Cypress, Puppeteer

### Python
- **Unit**: pytest, unittest
- **Integration**: pytest with fixtures
- **E2E**: Selenium, Playwright

### C++
- **Unit**: Google Test (gtest), Catch2
- **Integration**: Custom test harnesses
- **E2E**: Qt Test for GUI applications

## Test Organization

### File Structure
```
src/
  components/
    Button/
      Button.tsx
      Button.test.tsx
tests/
  integration/
    api.test.js
  e2e/
    user-registration.spec.js
```

### Naming Conventions
- Test files: `*.test.js`, `*.spec.js`
- Test functions: `describe('ComponentName', () => { ... })`
- Test cases: `it('should do something', () => { ... })`

## Continuous Integration

### CI Pipeline Integration
- Run unit tests on every commit
- Run integration tests on pull requests
- Run E2E tests nightly or on releases
- Generate coverage reports
- Fail builds on test failures

### Example GitHub Actions
```yaml
- name: Run tests
  run: npm test -- --coverage
  
- name: Upload coverage
  uses: codecov/codecov-action@v3
  with:
    file: ./coverage/lcov.info
```

## Test-Driven Development (TDD)

### Red-Green-Refactor Cycle
1. **Red**: Write failing test
2. **Green**: Write minimal code to pass test
3. **Refactor**: Improve code while keeping tests passing

### Benefits
- Ensures testable code
- Prevents regression bugs
- Improves design decisions
- Increases confidence in changes

## Performance Testing

### Types
- **Load Testing**: Test under normal load
- **Stress Testing**: Test under extreme load
- **Spike Testing**: Test sudden load increases

### Tools
- JMeter, k6, Artillery for API load testing
- Lighthouse for web performance
- Browser DevTools for profiling

## Security Testing

### Best Practices
- Test for common vulnerabilities (OWASP Top 10)
- Use static analysis tools (SAST)
- Perform dependency scanning
- Test authentication and authorization
- Validate input sanitization

## Accessibility Testing

### Guidelines
- Test with screen readers
- Check keyboard navigation
- Validate color contrast
- Test responsive design
- Use automated tools (axe-core, lighthouse)

## Common Testing Anti-Patterns

### ❌ Avoid These
- Testing implementation details
- Slow, flaky tests
- Tests that depend on each other
- Over-mocking (testing mocks instead of code)
- Ignoring test maintenance

### ✅ Do These
- Test behavior, not implementation
- Keep tests fast and reliable
- Isolate test cases
- Use realistic test data
- Maintain and refactor tests regularly

## Metrics and Monitoring

### Key Metrics
- Test coverage percentage
- Test execution time
- Flakiness rate
- Number of failing tests

### Tools
- Codecov, Coveralls for coverage
- Test analytics platforms
- CI/CD dashboards

## Conclusion

Effective testing strategies are crucial for delivering high-quality software. Focus on the testing pyramid, write maintainable tests, and integrate testing into your development workflow. Remember: tests are code too - keep them clean, well-organized, and regularly maintained.