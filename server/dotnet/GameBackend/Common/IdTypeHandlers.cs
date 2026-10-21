using System.Data;
using Dapper;

namespace Sa.GameBackend.Common;

/// <summary>Teaches Dapper to read and write the strongly typed ids as plain bigint and integer columns.</summary>
internal static class IdTypeHandlers
{
    private static int _registered;

    public static void Register()
    {
        if (Interlocked.Exchange(ref _registered, 1) == 1)
        {
            return;
        }

        SqlMapper.AddTypeHandler(new Int64WrapperHandler<AccountId>(v => new AccountId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int64WrapperHandler<WorldId>(v => new WorldId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int64WrapperHandler<CharacterId>(v => new CharacterId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int64WrapperHandler<OwnerEpoch>(v => new OwnerEpoch(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int32WrapperHandler<ZoneId>(v => new ZoneId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int32WrapperHandler<ClassId>(v => new ClassId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int32WrapperHandler<ItemTemplateId>(v => new ItemTemplateId(v), id => id.Value));
        SqlMapper.AddTypeHandler(new Int32WrapperHandler<MonsterTemplateId>(v => new MonsterTemplateId(v), id => id.Value));
    }

    private sealed class Int64WrapperHandler<T>(Func<long, T> wrap, Func<T, long> unwrap) : SqlMapper.TypeHandler<T>
        where T : struct
    {
        public override T Parse(object value) => wrap(Convert.ToInt64(value, System.Globalization.CultureInfo.InvariantCulture));

        public override void SetValue(IDbDataParameter parameter, T value)
        {
            parameter.DbType = DbType.Int64;
            parameter.Value = unwrap(value);
        }
    }

    private sealed class Int32WrapperHandler<T>(Func<int, T> wrap, Func<T, int> unwrap) : SqlMapper.TypeHandler<T>
        where T : struct
    {
        public override T Parse(object value) => wrap(Convert.ToInt32(value, System.Globalization.CultureInfo.InvariantCulture));

        public override void SetValue(IDbDataParameter parameter, T value)
        {
            parameter.DbType = DbType.Int32;
            parameter.Value = unwrap(value);
        }
    }
}
