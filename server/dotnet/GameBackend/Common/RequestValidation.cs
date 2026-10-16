using System.ComponentModel.DataAnnotations;
using Microsoft.AspNetCore.Http.HttpResults;

namespace Sa.GameBackend.Common;

/// <summary>Validates a request DTO with its DataAnnotations and shapes failures as RFC 9457 problem details.</summary>
internal static class RequestValidation
{
    public static ValidationProblem? Validate(object request)
    {
        var results = new List<ValidationResult>();
        if (Validator.TryValidateObject(request, new ValidationContext(request), results, validateAllProperties: true))
        {
            return null;
        }

        Dictionary<string, string[]> errors = results
            .SelectMany(r => r.MemberNames.DefaultIfEmpty(string.Empty), (r, member) => (member, r.ErrorMessage ?? "invalid"))
            .GroupBy(x => x.member, x => x.Item2, StringComparer.Ordinal)
            .ToDictionary(g => g.Key, g => g.ToArray(), StringComparer.Ordinal);
        return TypedResults.ValidationProblem(errors);
    }
}
